#include "asset_manager.h"
#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_prefab.h"

typedef enum {
  PrefabRes_MapAcquired  = 1 << 0,
  PrefabRes_MapUnloading = 1 << 1,
} PrefabResFlags;

ecs_comp_define(ScenePrefabResourceComp) {
  PrefabResFlags flags;
  String         mapId;
  EcsEntityId    mapEntity;
};

static void ecs_destruct_prefab_resource(void* data) {
  ScenePrefabResourceComp* comp = data;
  string_free(g_alloc_heap, comp->mapId);
}

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GlobalResourceView) { ecs_access_write(ScenePrefabResourceComp); }

static AssetManagerComp* scene_prefab_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static ScenePrefabResourceComp* scene_prefab_resource(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, ScenePrefabResourceComp) : null;
}

ecs_system_define(ScenePrefabInitMapSys) {
  AssetManagerComp*        assets   = scene_prefab_asset_manager(world);
  ScenePrefabResourceComp* resource = scene_prefab_resource(world);
  if (!assets || !resource) {
    return;
  }

  if (!resource->mapEntity) {
    resource->mapEntity = asset_lookup(world, assets, resource->mapId);
  }

  if (!(resource->flags & (PrefabRes_MapAcquired | PrefabRes_MapUnloading))) {
    log_i("Acquiring prefab-map", log_param("id", fmt_text(resource->mapId)));
    asset_acquire(world, resource->mapEntity);
    resource->flags |= PrefabRes_MapAcquired;
  }
}

ecs_system_define(ScenePrefabUnloadChangedMapSys) {
  ScenePrefabResourceComp* resource = scene_prefab_resource(world);
  if (!resource || !ecs_entity_valid(resource->mapEntity)) {
    return;
  }
  const bool isLoaded   = ecs_world_has_t(world, resource->mapEntity, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, resource->mapEntity, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, resource->mapEntity, AssetChangedComp);

  if (resource->flags & PrefabRes_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading prefab-map",
        log_param("id", fmt_text(resource->mapId)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, resource->mapEntity);
    resource->flags &= ~PrefabRes_MapAcquired;
    resource->flags |= PrefabRes_MapUnloading;
  }
  if (resource->flags & PrefabRes_MapUnloading && !isLoaded) {
    resource->flags &= ~PrefabRes_MapUnloading;
  }
}

ecs_module_init(scene_prefab_module) {
  ecs_register_comp(ScenePrefabResourceComp, .destructor = ecs_destruct_prefab_resource);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalResourceView);

  ecs_register_system(
      ScenePrefabInitMapSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GlobalResourceView));
  ecs_register_system(ScenePrefabUnloadChangedMapSys, ecs_view_id(GlobalResourceView));
}

void scene_prefab_init(EcsWorld* world, const String prefabMapId) {
  diag_assert_msg(prefabMapId.size, "Invalid prefabMapId");

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      ScenePrefabResourceComp,
      .mapId = string_dup(g_alloc_heap, prefabMapId));
}
