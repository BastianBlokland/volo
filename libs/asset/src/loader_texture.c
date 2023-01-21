#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_string.h"
#include "ecs_world.h"
#include "geo_vector.h"

#include "repo_internal.h"

ecs_comp_define_public(AssetTextureComp);

static void ecs_destruct_texture_comp(void* data) {
  AssetTextureComp* comp = data;
  alloc_free(g_alloc_heap, asset_texture_data(comp));
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetTextureComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any texture-asset components for unloaded assets.
 */
ecs_system_define(UnloadTextureAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetTextureComp);
  }
}

ecs_module_init(asset_texture_module) {
  ecs_register_comp(AssetTextureComp, .destructor = ecs_destruct_texture_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadTextureAssetSys, ecs_view_id(UnloadView));
}

String asset_texture_type_str(const AssetTextureType type) {
  static const String g_names[] = {
      string_static("U8"),
      string_static("U16"),
      string_static("F32"),
  };
  ASSERT(array_elems(g_names) == AssetTextureType_Count, "Incorrect number of names");
  return g_names[type];
}

usize asset_texture_pixel_size(const AssetTextureComp* texture) {
  switch (texture->type) {
  case AssetTextureType_U8:
    return sizeof(u8) * texture->channels;
  case AssetTextureType_U16:
    return sizeof(u16) * texture->channels;
  case AssetTextureType_F32:
    return sizeof(f32) * texture->channels;
  case AssetTextureType_Count:
    UNREACHABLE
  }
  diag_crash();
}

Mem asset_texture_data(const AssetTextureComp* texture) {
  const usize pixelCount = texture->width * texture->height;
  const usize layerCount = math_max(1, texture->layers);
  const usize dataSize   = asset_texture_pixel_size(texture) * pixelCount * layerCount;
  return mem_create(texture->pixelsRaw, dataSize);
}

GeoColor asset_texture_at(const AssetTextureComp* tex, const u32 layer, const usize index) {
  const usize pixelCount    = tex->width * tex->height;
  const usize layerDataSize = pixelCount * asset_texture_pixel_size(tex);
  const void* pixels        = tex->pixelsRaw + (layerDataSize * layer);

  GeoColor res;
  switch (tex->type) {
  // 8 bit unsigned pixels.
  case AssetTextureType_U8: {
    static const f32 g_u8MaxInv = 1.0f / u8_max;
    switch (tex->channels) {
    case AssetTextureChannels_One:
      res.r = 1.0f;
      res.g = 1.0f;
      res.b = 1.0f;
      res.a = ((AssetTexturePixelB1*)pixels)[index].r * g_u8MaxInv;
      goto ColorDecode;
    case AssetTextureChannels_Four:
      res.r = ((AssetTexturePixelB4*)pixels)[index].r * g_u8MaxInv;
      res.g = ((AssetTexturePixelB4*)pixels)[index].g * g_u8MaxInv;
      res.b = ((AssetTexturePixelB4*)pixels)[index].b * g_u8MaxInv;
      res.a = ((AssetTexturePixelB4*)pixels)[index].a * g_u8MaxInv;
      goto ColorDecode;
    }
  }
  // 16 bit unsigned pixels.
  case AssetTextureType_U16: {
    static const f32 g_u16MaxInv = 1.0f / u16_max;
    switch (tex->channels) {
    case AssetTextureChannels_One:
      res.r = 1.0f;
      res.g = 1.0f;
      res.b = 1.0f;
      res.a = ((AssetTexturePixelU1*)pixels)[index].r * g_u16MaxInv;
      goto ColorDecode;
    case AssetTextureChannels_Four:
      res.r = ((AssetTexturePixelU4*)pixels)[index].r * g_u16MaxInv;
      res.g = ((AssetTexturePixelU4*)pixels)[index].g * g_u16MaxInv;
      res.b = ((AssetTexturePixelU4*)pixels)[index].b * g_u16MaxInv;
      res.a = ((AssetTexturePixelU4*)pixels)[index].a * g_u16MaxInv;
      goto ColorDecode;
    }
  }
  // 32 bit floating point pixels.
  case AssetTextureType_F32: {
    switch (tex->channels) {
    case AssetTextureChannels_One:
      res.r = 1.0f;
      res.g = 1.0f;
      res.b = 1.0f;
      res.a = ((AssetTexturePixelF1*)pixels)[index].r;
      goto ColorDecode;
    case AssetTextureChannels_Four:
      res.r = ((AssetTexturePixelF4*)pixels)[index].r;
      res.g = ((AssetTexturePixelF4*)pixels)[index].g;
      res.b = ((AssetTexturePixelF4*)pixels)[index].b;
      res.a = ((AssetTexturePixelF4*)pixels)[index].a;
      goto ColorDecode;
    }
  }
  case AssetTextureType_Count:
    break;
  }
  UNREACHABLE

ColorDecode:
  if (tex->flags & AssetTextureFlags_Srgb) {
    // Simple approximation of the srgb curve: https://en.wikipedia.org/wiki/SRGB.
    res.r = math_pow_f32(res.r, 2.2f);
    res.g = math_pow_f32(res.g, 2.2f);
    res.b = math_pow_f32(res.b, 2.2f);
    res.a = math_pow_f32(res.a, 2.2f);
  }
  return res;
}

GeoColor asset_texture_sample(
    const AssetTextureComp* tex, const f32 xNorm, const f32 yNorm, const u32 layer) {
  diag_assert(xNorm >= 0.0 && xNorm <= 1.0f);
  diag_assert(yNorm >= 0.0 && yNorm <= 1.0f);
  diag_assert(layer < math_max(1, tex->layers));

  const f32 x = xNorm * (tex->width - 1), y = yNorm * (tex->height - 1);

  const f32 corner1x = math_min(tex->width - 2, math_round_down_f32(x));
  const f32 corner1y = math_min(tex->height - 2, math_round_down_f32(y));
  const f32 corner2x = corner1x + 1.0f, corner2y = corner1y + 1.0f;

  const GeoColor c1 = asset_texture_at(tex, layer, (usize)corner1y * tex->width + (usize)corner1x);
  const GeoColor c2 = asset_texture_at(tex, layer, (usize)corner1y * tex->width + (usize)corner2x);
  const GeoColor c3 = asset_texture_at(tex, layer, (usize)corner2y * tex->width + (usize)corner1x);
  const GeoColor c4 = asset_texture_at(tex, layer, (usize)corner2y * tex->width + (usize)corner2x);

  return geo_color_bilerp(c1, c2, c3, c4, x - corner1x, y - corner1y);
}

bool asset_texture_is_normalmap(const String id) {
  static const String g_patterns[] = {
      string_static("*_nrm.*"),
      string_static("*_normal.*"),
  };
  array_for_t(g_patterns, String, pattern) {
    if (string_match_glob(id, *pattern, StringMatchFlags_IgnoreCase)) {
      return true;
    }
  }
  return false;
}
