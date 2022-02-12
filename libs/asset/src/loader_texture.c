#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_string.h"
#include "ecs_world.h"

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
  case AssetTextureType_Float:
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
