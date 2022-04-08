#include "asset_manager.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "resource_internal.h"

static const String g_inputGlobalMap = string_static("input/global.imp");

typedef enum {
  InputRes_MapAcquired  = 1 << 0,
  InputRes_MapUnloading = 1 << 1,
} InputResFlags;

ecs_comp_define(InputResourcesComp) {
  InputResFlags flags;
  EcsEntityId   map;
};

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GlobalResourcesView) { ecs_access_write(InputResourcesComp); }

static AssetManagerComp* input_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static InputResourcesComp* input_resources(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourcesView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, InputResourcesComp) : null;
}

ecs_system_define(InputResourceInitSys) {
  AssetManagerComp* assets = input_asset_manager(world);
  if (!assets) {
    return; // Asset manager hasn't been initialized yet.
  }

  InputResourcesComp* resources = input_resources(world);
  if (!resources) {
    // Initialize global resources lookup.
    ecs_world_add_t(
        world,
        ecs_world_global(world),
        InputResourcesComp,
        .map = asset_lookup(world, assets, g_inputGlobalMap));
    return;
  }

  if (!(resources->flags & (InputRes_MapAcquired | InputRes_MapUnloading))) {
    log_i("Acquiring global inputmap", log_param("id", fmt_text(g_inputGlobalMap)));
    asset_acquire(world, resources->map);
    resources->flags |= InputRes_MapAcquired;
  }
}

ecs_system_define(InputResourceUnloadChangedMapSys) {
  InputResourcesComp* resources = input_resources(world);
  if (!resources) {
    return;
  }
  const bool isLoaded   = ecs_world_has_t(world, resources->map, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, resources->map, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, resources->map, AssetChangedComp);

  if (resources->flags & InputRes_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading global inputmap",
        log_param("id", fmt_text(g_inputGlobalMap)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, resources->map);
    resources->flags &= ~InputRes_MapAcquired;
    resources->flags |= InputRes_MapUnloading;
  }
  if (resources->flags & InputRes_MapUnloading && !isLoaded) {
    resources->flags &= ~InputRes_MapUnloading;
  }
}

ecs_module_init(input_resource_module) {
  ecs_register_comp(InputResourcesComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalResourcesView);

  ecs_register_system(
      InputResourceInitSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GlobalResourcesView));
  ecs_register_system(InputResourceUnloadChangedMapSys, ecs_view_id(GlobalResourcesView));
}

EcsEntityId input_resource_map(const InputResourcesComp* comp) { return comp->map; }
