#include "asset_raw.h"
#include "core_alloc.h"
#include "ecs_world.h"

#include "loader_internal.h"

ecs_comp_define_public(AssetRawComp);
ecs_comp_define(AssetRawSourceComp) { AssetSource* src; };

static void ecs_destruct_source_comp(void* data) {
  AssetRawSourceComp* comp = data;
  asset_source_close(comp->src);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetRawComp);
  ecs_access_with(AssetRawSourceComp);
  ecs_access_without(AssetLoadedComp);
};

ecs_system_define(UnloadRawAssetsSys) {
  /**
   * Remove any raw-asset components for unloaded assets.
   */
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetRawComp);
    ecs_world_remove_t(world, entity, AssetRawSourceComp);
  }
}

ecs_module_init(asset_raw_module) {
  ecs_register_comp(AssetRawComp);
  ecs_register_comp(AssetRawSourceComp, .destructor = ecs_destruct_source_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadRawAssetsSys, ecs_view_id(UnloadView));
}

void asset_load_raw(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  ecs_world_add_t(world, assetEntity, AssetRawComp, .data = src->data);
  ecs_world_add_t(world, assetEntity, AssetRawSourceComp, .src = src);
  ecs_world_add_empty_t(world, assetEntity, AssetLoadedComp);
}
