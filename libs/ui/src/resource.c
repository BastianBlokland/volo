#include "asset_manager.h"
#include "core_array.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "snd_mixer.h"

#include "resource_internal.h"

static const String g_uiAtlasIds[UiAtlasType_Count] = {
    [UiAtlasType_Font]  = string_static("fonts/ui.fonttex"),
    [UiAtlasType_Image] = string_static("textures/ui/image.atlas"),
};
static const String g_uiGlobalGraphic      = string_static("graphics/ui/canvas.graphic");
static const String g_uiGlobalGraphicDebug = string_static("graphics/ui/canvas_debug.graphic");
static const String g_uiSoundClick         = string_static("external/sound/click-02.wav");
static const String g_uiSoundClickAlt      = string_static("external/sound/click-03.wav");

static const String g_uiAtlasTypeNames[] = {
    string_static("font"),
    string_static("image"),
};
ASSERT(array_elems(g_uiAtlasTypeNames) == UiAtlasType_Count, "Incorrect number of names");

ecs_comp_define(UiGlobalResourcesComp) {
  EcsEntityId atlases[UiAtlasType_Count];
  u32         acquiredAtlases;
  u32         unloadingAtlases;
  EcsEntityId graphic, graphicDebug;
  EcsEntityId soundClick, soundClickAlt;
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
    // Initialize global resources.
    globalResources = ecs_world_add_t(
        world,
        ecs_world_global(world),
        UiGlobalResourcesComp,
        .graphic       = asset_lookup(world, assets, g_uiGlobalGraphic),
        .graphicDebug  = asset_lookup(world, assets, g_uiGlobalGraphicDebug),
        .soundClick    = asset_lookup(world, assets, g_uiSoundClick),
        .soundClickAlt = asset_lookup(world, assets, g_uiSoundClickAlt));

    // Initialize atlases.
    for (UiAtlasType type = 0; type != UiAtlasType_Count; ++type) {
      globalResources->atlases[type] = asset_lookup(world, assets, g_uiAtlasIds[type]);
    }

    // Initialize sound assets.
    snd_mixer_persistent_asset(soundMixer, globalResources->soundClick);
    snd_mixer_persistent_asset(soundMixer, globalResources->soundClickAlt);
    return;
  }

  for (UiAtlasType type = 0; type != UiAtlasType_Count; ++type) {
    const bool isAcquired  = (globalResources->acquiredAtlases & (1 << type)) != 0;
    const bool isUnloading = (globalResources->unloadingAtlases & (1 << type)) != 0;
    if (!isAcquired && !isUnloading) {
      log_i(
          "Acquiring ui {} atlas",
          log_param("type", fmt_text(g_uiAtlasTypeNames[type])),
          log_param("id", fmt_text(g_uiAtlasIds[type])));
      asset_acquire(world, globalResources->atlases[type]);
      globalResources->acquiredAtlases |= 1 << type;
    }
  }
}

ecs_system_define(UiResourceUnloadChangedAtlasSys) {
  UiGlobalResourcesComp* globalResources = ui_global_resources(world);
  if (!globalResources) {
    return;
  }
  for (UiAtlasType type = 0; type != UiAtlasType_Count; ++type) {
    const EcsEntityId atlas      = globalResources->atlases[type];
    const bool        isAcquired = (globalResources->acquiredAtlases & (1 << type)) != 0;
    const bool        isLoaded   = ecs_world_has_t(world, atlas, AssetLoadedComp);
    const bool        isFailed   = ecs_world_has_t(world, atlas, AssetFailedComp);
    const bool        hasChanged = ecs_world_has_t(world, atlas, AssetChangedComp);

    if (isAcquired && (isLoaded || isFailed) && hasChanged) {
      log_i(
          "Unloading ui {} atlas",
          log_param("type", fmt_text(g_uiAtlasTypeNames[type])),
          log_param("id", fmt_text(g_uiAtlasIds[type])),
          log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, atlas);
      globalResources->acquiredAtlases &= ~(1 << type);
      globalResources->unloadingAtlases |= 1 << type;
    }

    const bool isUnloading = (globalResources->unloadingAtlases & (1 << type)) != 0;
    if (isUnloading && !isLoaded) {
      globalResources->unloadingAtlases &= ~(1 << type);
    }
  }
}

ecs_module_init(ui_resource_module) {
  ecs_register_comp(UiGlobalResourcesComp);

  ecs_register_view(GlobalInitView);
  ecs_register_view(GlobalResourcesView);

  ecs_register_system(
      UiResourceInitSys, ecs_view_id(GlobalInitView), ecs_view_id(GlobalResourcesView));
  ecs_register_system(UiResourceUnloadChangedAtlasSys, ecs_view_id(GlobalResourcesView));
}

EcsEntityId ui_resource_atlas(const UiGlobalResourcesComp* comp, const UiAtlasType type) {
  return comp->atlases[type];
}
EcsEntityId ui_resource_graphic(const UiGlobalResourcesComp* comp) { return comp->graphic; }
EcsEntityId ui_resource_graphic_debug(const UiGlobalResourcesComp* comp) {
  return comp->graphicDebug;
}
EcsEntityId ui_resource_sound_click(const UiGlobalResourcesComp* comp) { return comp->soundClick; }
EcsEntityId ui_resource_sound_click_alt(const UiGlobalResourcesComp* comp) {
  return comp->soundClickAlt;
}
