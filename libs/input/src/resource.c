#include "asset_manager.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "resource_internal.h"

static const String g_inputGlobalMap = string_static("input/global.imp");

typedef enum {
  InputGlobalRes_MapAcquired  = 1 << 0,
  InputGlobalRes_MapUnloading = 1 << 1,
} InputGlobalResFlags;

ecs_comp_define(InputGlobalResourcesComp) {
  InputGlobalResFlags flags;
  EcsEntityId         map;
};

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GlobalResourcesView) { ecs_access_write(InputGlobalResourcesComp); }

static AssetManagerComp* input_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static InputGlobalResourcesComp* input_global_resources(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourcesView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, InputGlobalResourcesComp) : null;
}

ecs_system_define(InputResourceInitSys) {
  AssetManagerComp* assets = input_asset_manager(world);
  if (!assets) {
    return; // Asset manager hasn't been initialized yet.
  }

  InputGlobalResourcesComp* globalResources = input_global_resources(world);
  if (!globalResources) {
    // Initialize global resources lookup.
    ecs_world_add_t(
        world,
        ecs_world_global(world),
        InputGlobalResourcesComp,
        .map = asset_lookup(world, assets, g_inputGlobalMap));
    return;
  }

  if (!(globalResources->flags & (InputGlobalRes_MapAcquired | InputGlobalRes_MapUnloading))) {
    log_i("Acquiring global inputmap", log_param("id", fmt_text(g_inputGlobalMap)));
    asset_acquire(world, globalResources->map);
    globalResources->flags |= InputGlobalRes_MapAcquired;
  }
}

ecs_system_define(InputResourceUnloadChangedMapSys) {
  InputGlobalResourcesComp* globalResources = input_global_resources(world);
  if (!globalResources) {
    return;
  }
  const bool isLoaded   = ecs_world_has_t(world, globalResources->map, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, globalResources->map, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, globalResources->map, AssetChangedComp);

  if (globalResources->flags & InputGlobalRes_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading global inputmap",
        log_param("id", fmt_text(g_inputGlobalMap)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, globalResources->map);
    globalResources->flags &= ~InputGlobalRes_MapAcquired;
    globalResources->flags |= InputGlobalRes_MapUnloading;
  }
  if (globalResources->flags & InputGlobalRes_MapUnloading && !isLoaded) {
    globalResources->flags &= ~InputGlobalRes_MapUnloading;
  }
}

ecs_module_init(input_resource_module) {
  ecs_register_comp(InputGlobalResourcesComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalResourcesView);

  ecs_register_system(
      InputResourceInitSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GlobalResourcesView));
  ecs_register_system(InputResourceUnloadChangedMapSys, ecs_view_id(GlobalResourcesView));
}

EcsEntityId input_resource_map(const InputGlobalResourcesComp* comp) { return comp->map; }
