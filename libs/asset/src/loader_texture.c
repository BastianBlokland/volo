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

usize asset_texture_req_mip_size(
    const AssetTextureType     type,
    const AssetTextureChannels channels,
    const u32                  width,
    const u32                  height,
    const u32                  layers,
    const u32                  mipLevel) {
  const u32 mipWidth  = math_max(width >> mipLevel, 1);
  const u32 mipHeight = math_max(height >> mipLevel, 1);
  switch (type) {
  case AssetTextureType_U8:
    return sizeof(u8) * channels * mipWidth * mipHeight * math_max(1, layers);
  case AssetTextureType_U16:
    return sizeof(u16) * channels * mipWidth * mipHeight * math_max(1, layers);
  case AssetTextureType_F32:
    return sizeof(f32) * channels * mipWidth * mipHeight * math_max(1, layers);
  case AssetTextureType_Count:
    UNREACHABLE
  }
  diag_crash();
}

usize asset_texture_req_size(
    const AssetTextureType     type,
    const AssetTextureChannels channels,
    const u32                  width,
    const u32                  height,
    const u32                  layers,
    const u32                  mipLevels) {
  usize size = 0;
  for (u32 mipLevel = 0; mipLevel != math_max(mipLevels, 1); ++mipLevel) {
    size += asset_texture_req_mip_size(type, channels, width, height, layers, mipLevel);
  }
  return size;
}

usize asset_texture_req_align(const AssetTextureType type, const AssetTextureChannels channels) {
  switch (type) {
  case AssetTextureType_U8:
    return sizeof(u8) * channels;
  case AssetTextureType_U16:
    return sizeof(u16) * channels;
  case AssetTextureType_F32:
    return sizeof(f32) * channels;
  case AssetTextureType_Count:
    UNREACHABLE
  }
  diag_crash();
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

usize asset_texture_mip_size(const AssetTextureComp* texture, const u32 mipLevel) {
  diag_assert(mipLevel < math_max(texture->srcMipLevels, 1));
  return asset_texture_req_mip_size(
      texture->type, texture->channels, texture->width, texture->height, texture->layers, mipLevel);
}

usize asset_texture_data_size(const AssetTextureComp* texture) {
  return asset_texture_req_size(
      texture->type,
      texture->channels,
      texture->width,
      texture->height,
      texture->layers,
      texture->srcMipLevels);
}

Mem asset_texture_data(const AssetTextureComp* texture) {
  return mem_create(texture->pixelsRaw, asset_texture_data_size(texture));
}

GeoColor asset_texture_at(const AssetTextureComp* tex, const u32 layer, const usize index) {
  const usize pixelCount    = tex->width * tex->height;
  const usize layerDataSize = pixelCount * asset_texture_pixel_size(tex);
  const void* pixelsMip0    = tex->pixelsRaw + (layerDataSize * layer);

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
      res.a = ((AssetTexturePixelB1*)pixelsMip0)[index].r * g_u8MaxInv;
      goto ColorDecode;
    case AssetTextureChannels_Four:
      res.r = ((AssetTexturePixelB4*)pixelsMip0)[index].r * g_u8MaxInv;
      res.g = ((AssetTexturePixelB4*)pixelsMip0)[index].g * g_u8MaxInv;
      res.b = ((AssetTexturePixelB4*)pixelsMip0)[index].b * g_u8MaxInv;
      res.a = ((AssetTexturePixelB4*)pixelsMip0)[index].a * g_u8MaxInv;
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
      res.a = ((AssetTexturePixelU1*)pixelsMip0)[index].r * g_u16MaxInv;
      goto ColorDecode;
    case AssetTextureChannels_Four:
      res.r = ((AssetTexturePixelU4*)pixelsMip0)[index].r * g_u16MaxInv;
      res.g = ((AssetTexturePixelU4*)pixelsMip0)[index].g * g_u16MaxInv;
      res.b = ((AssetTexturePixelU4*)pixelsMip0)[index].b * g_u16MaxInv;
      res.a = ((AssetTexturePixelU4*)pixelsMip0)[index].a * g_u16MaxInv;
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
      res.a = ((AssetTexturePixelF1*)pixelsMip0)[index].r;
      goto ColorDecode;
    case AssetTextureChannels_Four:
      res.r = ((AssetTexturePixelF4*)pixelsMip0)[index].r;
      res.g = ((AssetTexturePixelF4*)pixelsMip0)[index].g;
      res.b = ((AssetTexturePixelF4*)pixelsMip0)[index].b;
      res.a = ((AssetTexturePixelF4*)pixelsMip0)[index].a;
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
