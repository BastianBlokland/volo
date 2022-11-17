#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_weapon.h"

typedef enum {
  WeaponRes_MapAcquired  = 1 << 0,
  WeaponRes_MapUnloading = 1 << 1,
} WeaponResFlags;

ecs_comp_define(SceneWeaponResourceComp) {
  WeaponResFlags flags;
  String         mapId;
  EcsEntityId    mapEntity;
};

static void ecs_destruct_weapon_resource(void* data) {
  SceneWeaponResourceComp* comp = data;
  string_free(g_alloc_heap, comp->mapId);
}

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GlobalResourceView) { ecs_access_write(SceneWeaponResourceComp); }

static AssetManagerComp* weapon_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static SceneWeaponResourceComp* weapon_resource(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, SceneWeaponResourceComp) : null;
}

ecs_system_define(SceneWeaponInitMapSys) {
  AssetManagerComp*        assets   = weapon_asset_manager(world);
  SceneWeaponResourceComp* resource = weapon_resource(world);
  if (!assets || !resource) {
    return;
  }

  if (!resource->mapEntity) {
    resource->mapEntity = asset_lookup(world, assets, resource->mapId);
  }

  if (!(resource->flags & (WeaponRes_MapAcquired | WeaponRes_MapUnloading))) {
    log_i("Acquiring weapon-map", log_param("id", fmt_text(resource->mapId)));
    asset_acquire(world, resource->mapEntity);
    resource->flags |= WeaponRes_MapAcquired;
  }
}

ecs_system_define(SceneWeaponUnloadChangedMapSys) {
  SceneWeaponResourceComp* resource = weapon_resource(world);
  if (!resource || !ecs_entity_valid(resource->mapEntity)) {
    return;
  }
  const bool isLoaded   = ecs_world_has_t(world, resource->mapEntity, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, resource->mapEntity, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, resource->mapEntity, AssetChangedComp);

  if (resource->flags & WeaponRes_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading weapon-map",
        log_param("id", fmt_text(resource->mapId)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, resource->mapEntity);
    resource->flags &= ~WeaponRes_MapAcquired;
    resource->flags |= WeaponRes_MapUnloading;
  }
  if (resource->flags & WeaponRes_MapUnloading && !isLoaded) {
    resource->flags &= ~WeaponRes_MapUnloading;
  }
}

ecs_module_init(scene_weapon_module) {
  ecs_register_comp(SceneWeaponResourceComp, .destructor = ecs_destruct_weapon_resource);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalResourceView);

  ecs_register_system(
      SceneWeaponInitMapSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GlobalResourceView));
  ecs_register_system(SceneWeaponUnloadChangedMapSys, ecs_view_id(GlobalResourceView));
}

void scene_weapon_init(EcsWorld* world, const String weaponMapId) {
  diag_assert_msg(weaponMapId.size, "Invalid weaponMapId");

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneWeaponResourceComp,
      .mapId = string_dup(g_alloc_heap, weaponMapId));
}

EcsEntityId scene_weapon_map(const SceneWeaponResourceComp* comp) { return comp->mapEntity; }
