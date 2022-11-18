#include "asset_manager.h"
#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_prefab.h"

typedef enum {
  PrefabResource_MapAcquired  = 1 << 0,
  PrefabResource_MapUnloading = 1 << 1,
} PrefabResourceFlags;

ecs_comp_define(ScenePrefabResourceComp) {
  PrefabResourceFlags flags;
  String              mapId;
  EcsEntityId         mapEntity;
};

ecs_comp_define(ScenePrefabRequestComp) { ScenePrefabSpec spec; };

static void ecs_destruct_prefab_resource(void* data) {
  ScenePrefabResourceComp* comp = data;
  string_free(g_alloc_heap, comp->mapId);
}

ecs_view_define(GlobalResourceUpdateView) {
  ecs_access_write(ScenePrefabResourceComp);
  ecs_access_write(AssetManagerComp);
}
ecs_view_define(GlobalResourceReadView) { ecs_access_read(ScenePrefabResourceComp); }
ecs_view_define(PrefabMapAssetView) { ecs_access_read(AssetPrefabMapComp); }
ecs_view_define(PrefabSpawnView) { ecs_access_read(ScenePrefabRequestComp); }

ecs_system_define(ScenePrefabResourceInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*        assets   = ecs_view_write_t(globalItr, AssetManagerComp);
  ScenePrefabResourceComp* resource = ecs_view_write_t(globalItr, ScenePrefabResourceComp);

  if (!resource->mapEntity) {
    resource->mapEntity = asset_lookup(world, assets, resource->mapId);
  }

  if (!(resource->flags & (PrefabResource_MapAcquired | PrefabResource_MapUnloading))) {
    log_i("Acquiring prefab-map", log_param("id", fmt_text(resource->mapId)));
    asset_acquire(world, resource->mapEntity);
    resource->flags |= PrefabResource_MapAcquired;
  }
}

ecs_system_define(ScenePrefabResourceUnloadChangedSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  ScenePrefabResourceComp* resource = ecs_view_write_t(globalItr, ScenePrefabResourceComp);
  if (!resource->mapEntity) {
    return;
  }

  const bool isLoaded   = ecs_world_has_t(world, resource->mapEntity, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, resource->mapEntity, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, resource->mapEntity, AssetChangedComp);

  if (resource->flags & PrefabResource_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading prefab-map",
        log_param("id", fmt_text(resource->mapId)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, resource->mapEntity);
    resource->flags &= ~PrefabResource_MapAcquired;
    resource->flags |= PrefabResource_MapUnloading;
  }
  if (resource->flags & PrefabResource_MapUnloading && !isLoaded) {
    resource->flags &= ~PrefabResource_MapUnloading;
  }
}

ecs_system_define(ScenePrefabSpawnSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceReadView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const ScenePrefabResourceComp* resource = ecs_view_read_t(globalItr, ScenePrefabResourceComp);

  EcsView*     mapAssetView = ecs_world_view_t(world, PrefabMapAssetView);
  EcsIterator* mapAssetItr  = ecs_view_maybe_at(mapAssetView, resource->mapEntity);
  if (!mapAssetItr) {
    return;
  }
  const AssetPrefabMapComp* map = ecs_view_read_t(mapAssetItr, AssetPrefabMapComp);

  EcsView* spawnView = ecs_world_view_t(world, PrefabSpawnView);
  for (EcsIterator* itr = ecs_view_itr(spawnView); ecs_view_walk(itr);) {
    const EcsEntityId             entity  = ecs_view_entity(itr);
    const ScenePrefabRequestComp* request = ecs_view_read_t(globalItr, ScenePrefabRequestComp);
    const AssetPrefab*            prefab  = asset_prefab_get(map, request->spec.prefabId);
    if (UNLIKELY(!prefab)) {
      log_e("Prefab not found", log_param("entity", fmt_int(entity, .base = 16)));
      goto RequestHandled;
    }

  RequestHandled:
    ecs_world_remove_t(world, entity, ScenePrefabRequestComp);
  }
}

ecs_module_init(scene_prefab_module) {
  ecs_register_comp(ScenePrefabResourceComp, .destructor = ecs_destruct_prefab_resource);
  ecs_register_comp(ScenePrefabRequestComp, .destructor = ecs_destruct_prefab_resource);

  ecs_register_view(GlobalResourceUpdateView);
  ecs_register_view(GlobalResourceReadView);
  ecs_register_view(PrefabMapAssetView);
  ecs_register_view(PrefabSpawnView);

  ecs_register_system(ScenePrefabResourceInitSys, ecs_view_id(GlobalResourceUpdateView));

  ecs_register_system(ScenePrefabResourceUnloadChangedSys, ecs_view_id(GlobalResourceUpdateView));

  ecs_register_system(
      ScenePrefabSpawnSys,
      ecs_view_id(GlobalResourceReadView),
      ecs_view_id(PrefabMapAssetView),
      ecs_view_id(PrefabSpawnView));
}

void scene_prefab_init(EcsWorld* world, const String prefabMapId) {
  diag_assert_msg(prefabMapId.size, "Invalid prefabMapId");

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      ScenePrefabResourceComp,
      .mapId = string_dup(g_alloc_heap, prefabMapId));
}

EcsEntityId scene_prefab_spawn(EcsWorld* world, const ScenePrefabSpec* spec) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, ScenePrefabRequestComp, .spec = *spec);
  return e;
}
