#include "asset_manager.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "snd_mixer.h"

#include "resource_internal.h"

static const String g_uiGlobalFont         = string_static("fonts/ui.fonttex");
static const String g_uiGlobalGraphic      = string_static("graphics/ui/canvas.graphic");
static const String g_uiGlobalGraphicDebug = string_static("graphics/ui/canvas_debug.graphic");
static const String g_uiSoundClick         = string_static("external/sound/click-02.wav");
static const String g_uiSoundClickAlt      = string_static("external/sound/click-03.wav");

typedef enum {
  UiGlobalRes_FontAcquired  = 1 << 0,
  UiGlobalRes_FontUnloading = 1 << 1,
} UiGlobalResFlags;

ecs_comp_define(UiGlobalResourcesComp) {
  UiGlobalResFlags flags;
  EcsEntityId      font;
  EcsEntityId      graphic, graphicDebug;
  EcsEntityId      soundClick, soundClickAlt;
};

ecs_view_define(GlobalInitView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(SndMixerComp);
}

ecs_view_define(GlobalResourcesView) { ecs_access_write(UiGlobalResourcesComp); }

static UiGlobalResourcesComp* ui_global_resources(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourcesView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, UiGlobalResourcesComp) : null;
}

ecs_system_define(UiResourceInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalInitView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized yet.
  }
  AssetManagerComp* assets     = ecs_view_write_t(globalItr, AssetManagerComp);
  SndMixerComp*     soundMixer = ecs_view_write_t(globalItr, SndMixerComp);

  UiGlobalResourcesComp* globalResources = ui_global_resources(world);
  if (!globalResources) {
    // Initialize global resource lookup.
    globalResources = ecs_world_add_t(
        world,
        ecs_world_global(world),
        UiGlobalResourcesComp,
        .font          = asset_lookup(world, assets, g_uiGlobalFont),
        .graphic       = asset_lookup(world, assets, g_uiGlobalGraphic),
        .graphicDebug  = asset_lookup(world, assets, g_uiGlobalGraphicDebug),
        .soundClick    = asset_lookup(world, assets, g_uiSoundClick),
        .soundClickAlt = asset_lookup(world, assets, g_uiSoundClickAlt));

    // Initialize sound assets.
    snd_mixer_persistent_asset(soundMixer, globalResources->soundClick);
    snd_mixer_persistent_asset(soundMixer, globalResources->soundClickAlt);
    return;
  }

  if (!(globalResources->flags & (UiGlobalRes_FontAcquired | UiGlobalRes_FontUnloading))) {
    log_i("Acquiring global font", log_param("id", fmt_text(g_uiGlobalFont)));
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
        log_param("id", fmt_text(g_uiGlobalFont)),
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

  ecs_register_view(GlobalInitView);
  ecs_register_view(GlobalResourcesView);

  ecs_register_system(
      UiResourceInitSys, ecs_view_id(GlobalInitView), ecs_view_id(GlobalResourcesView));
  ecs_register_system(UiResourceUnloadChangedFontsSys, ecs_view_id(GlobalResourcesView));
}

EcsEntityId ui_resource_font(const UiGlobalResourcesComp* comp) { return comp->font; }
EcsEntityId ui_resource_graphic(const UiGlobalResourcesComp* comp) { return comp->graphic; }
EcsEntityId ui_resource_graphic_debug(const UiGlobalResourcesComp* comp) {
  return comp->graphicDebug;
}
EcsEntityId ui_resource_sound_click(const UiGlobalResourcesComp* comp) { return comp->soundClick; }
EcsEntityId ui_resource_sound_click_alt(const UiGlobalResourcesComp* comp) {
  return comp->soundClickAlt;
}
