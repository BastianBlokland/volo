#include "asset_manager.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "resource_internal.h"

static const String g_ui_global_font = string_static("fonts/ui.ftx");

typedef enum {
  UiGlobalRes_FontAcquired  = 1 << 0,
  UiGlobalRes_FontUnloading = 1 << 1,
} UiGlobalResFlags;

ecs_comp_define(UiGlobalResourcesComp) {
  UiGlobalResFlags flags;
  EcsEntityId      font;
};

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GlobalResourcesView) { ecs_access_write(UiGlobalResourcesComp); }

static AssetManagerComp* ui_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static UiGlobalResourcesComp* ui_global_resources(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourcesView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, UiGlobalResourcesComp) : null;
}

ecs_system_define(UiResourceInitSys) {
  AssetManagerComp* assets = ui_asset_manager(world);
  if (!assets) {
    return; // Asset manager hasn't been initialized yet.
  }

  UiGlobalResourcesComp* globalResources = ui_global_resources(world);
  if (!globalResources) {
    // Initialize global fonts lookup.
    ecs_world_add_t(
        world,
        ecs_world_global(world),
        UiGlobalResourcesComp,
        .font = asset_lookup(world, assets, g_ui_global_font));
    return;
  }

  if (!(globalResources->flags & (UiGlobalRes_FontAcquired | UiGlobalRes_FontUnloading))) {
    log_i("Acquiring global font", log_param("id", fmt_text(g_ui_global_font)));
    asset_acquire(world, globalResources->font);
    globalResources->flags |= UiGlobalRes_FontAcquired;
  }
}

ecs_system_define(UiResourceUnloadChangedFontsSys) {
  UiGlobalResourcesComp* globalResources = ui_global_resources(world);
  if (!globalResources) {
    return;
  }
  const bool isLoaded   = ecs_world_has_t(world, globalResources->font, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, globalResources->font, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, globalResources->font, AssetChangedComp);

  if (globalResources->flags & UiGlobalRes_FontAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading global font",
        log_param("id", fmt_text(g_ui_global_font)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, globalResources->font);
    globalResources->flags &= ~UiGlobalRes_FontAcquired;
    globalResources->flags |= UiGlobalRes_FontUnloading;
  }
  if (globalResources->flags & UiGlobalRes_FontUnloading && !isLoaded) {
    globalResources->flags &= ~UiGlobalRes_FontUnloading;
  }
}

ecs_module_init(ui_resource_module) {
  ecs_register_comp(UiGlobalResourcesComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalResourcesView);

  ecs_register_system(
      UiResourceInitSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GlobalResourcesView));
  ecs_register_system(UiResourceUnloadChangedFontsSys, ecs_view_id(GlobalResourcesView));
}

EcsEntityId ui_resource_font(const UiGlobalResourcesComp* globalResources) {
  return globalResources->font;
}
