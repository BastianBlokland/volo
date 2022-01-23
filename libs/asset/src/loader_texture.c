#include "asset_texture.h"
#include "core_alloc.h"
#include "ecs_world.h"

#include "repo_internal.h"

ecs_comp_define_public(AssetTextureComp);

static void ecs_destruct_texture_comp(void* data) {
  AssetTextureComp* comp       = data;
  const u32         pixelCount = comp->height * comp->width;
  alloc_free(g_alloc_heap, mem_create(comp->pixelsRaw, comp->channels * sizeof(u8) * pixelCount));
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
