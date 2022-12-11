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

usize asset_texture_pixel_size(const AssetTextureComp* texture) {
  switch (texture->type) {
  case AssetTextureType_Byte:
    return sizeof(u8) * texture->channels;
  case AssetTextureType_F32:
    return sizeof(f32) * texture->channels;
  }
  diag_crash();
}

Mem asset_texture_data(const AssetTextureComp* texture) {
  const usize pixelCount = texture->width * texture->height;
  const usize layerCount = math_max(1, texture->layers);
  const usize dataSize   = asset_texture_pixel_size(texture) * pixelCount * layerCount;
  return mem_create(texture->pixelsRaw, dataSize);
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

static GeoVector pixel_b4_to_vec(const AssetTexturePixelB4 p) {
  return geo_vector(p.r, p.g, p.b, p.a);
}

static AssetTexturePixelB4 pixel_b4_from_vec(const GeoVector v) {
  return (AssetTexturePixelB4){.r = (u8)v.x, .g = (u8)v.y, .b = (u8)v.z, .a = (u8)v.w};
}

AssetTexturePixelB1 asset_texture_sample_b1(
    const AssetTextureComp* texture, const f32 xNorm, const f32 yNorm, const u32 layer) {
  diag_assert(texture->type == AssetTextureType_Byte);
  diag_assert(texture->channels == AssetTextureChannels_One);
  diag_assert(xNorm >= 0.0 && xNorm <= 1.0f);
  diag_assert(yNorm >= 0.0 && yNorm <= 1.0f);
  diag_assert(layer < math_max(1, texture->layers));

  const usize                pixelCount    = texture->width * texture->height;
  const usize                layerDataSize = pixelCount * sizeof(AssetTexturePixelB1);
  const AssetTexturePixelB1* pixels        = texture->pixelsB1 + (layerDataSize * layer);

  const f32 x = xNorm * (texture->width - 1), y = yNorm * (texture->height - 1);

  const f32 corner1x = math_min(texture->width - 2, math_round_down_f32(x));
  const f32 corner1y = math_min(texture->height - 2, math_round_down_f32(y));
  const f32 corner2x = corner1x + 1.0f, corner2y = corner1y + 1.0f;

  const u8 p1 = pixels[(usize)corner1y * texture->width + (usize)corner1x].r;
  const u8 p2 = pixels[(usize)corner1y * texture->width + (usize)corner2x].r;
  const u8 p3 = pixels[(usize)corner2y * texture->width + (usize)corner1x].r;
  const u8 p4 = pixels[(usize)corner2y * texture->width + (usize)corner2x].r;

  const f32 tX = x - corner1x, tY = y - corner1y;
  return (AssetTexturePixelB1){(u8)math_lerp(math_lerp(p1, p2, tX), math_lerp(p3, p4, tX), tY)};
}

AssetTexturePixelB4 asset_texture_sample_b4(
    const AssetTextureComp* texture, const f32 xNorm, const f32 yNorm, const u32 layer) {
  diag_assert(texture->type == AssetTextureType_Byte);
  diag_assert(texture->channels == AssetTextureChannels_Four);
  diag_assert(xNorm >= 0.0 && xNorm <= 1.0f);
  diag_assert(yNorm >= 0.0 && yNorm <= 1.0f);
  diag_assert(layer < math_max(1, texture->layers));

  const usize                pixelCount    = texture->width * texture->height;
  const usize                layerDataSize = pixelCount * sizeof(AssetTexturePixelB4);
  const AssetTexturePixelB4* pixels        = texture->pixelsB4 + (layerDataSize * layer);

  const f32 x = xNorm * (texture->width - 1), y = yNorm * (texture->height - 1);

  const f32 corner1x = math_min(texture->width - 2, math_round_down_f32(x));
  const f32 corner1y = math_min(texture->height - 2, math_round_down_f32(y));
  const f32 corner2x = corner1x + 1.0f, corner2y = corner1y + 1.0f;

  const GeoVector v1 = pixel_b4_to_vec(pixels[(usize)corner1y * texture->width + (usize)corner1x]);
  const GeoVector v2 = pixel_b4_to_vec(pixels[(usize)corner1y * texture->width + (usize)corner2x]);
  const GeoVector v3 = pixel_b4_to_vec(pixels[(usize)corner2y * texture->width + (usize)corner1x]);
  const GeoVector v4 = pixel_b4_to_vec(pixels[(usize)corner2y * texture->width + (usize)corner2x]);
  return pixel_b4_from_vec(geo_vector_bilerp(v1, v2, v3, v4, x - corner1x, y - corner1y));
}
