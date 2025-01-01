#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "snd_mixer.h"

#include "resource_internal.h"

static const String g_uiAtlasIds[UiAtlasRes_Count] = {
    [UiAtlasRes_Font]  = string_static("fonts/ui.fonttex"),
    [UiAtlasRes_Image] = string_static("textures/ui/image.atlas"),
};
static const String g_uiGraphicIds[UiGraphicRes_Count] = {
    [UiGraphicRes_Normal] = string_static("graphics/ui/canvas.graphic"),
    [UiGraphicRes_Debug]  = string_static("graphics/ui/canvas_debug.graphic"),
};
static const String g_uiSoundIds[UiSoundRes_Count] = {
    [UiSoundRes_Click]    = string_static("external/sound/click-02.wav"),
    [UiSoundRes_ClickAlt] = string_static("external/sound/click-03.wav"),
};

static const String g_uiAtlasResNames[] = {
    string_static("font"),
    string_static("image"),
};
ASSERT(array_elems(g_uiAtlasResNames) == UiAtlasRes_Count, "Incorrect number of names");

ecs_comp_define(UiGlobalResourcesComp) {
  EcsEntityId atlases[UiAtlasRes_Count];
  u32         acquiredAtlases;
  u32         unloadingAtlases;
  EcsEntityId graphics[UiGraphicRes_Count];
  EcsEntityId sounds[UiSoundRes_Count];
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

ecs_system_define(UiResourceUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalInitView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized yet.
  }
  AssetManagerComp* assets     = ecs_view_write_t(globalItr, AssetManagerComp);
  SndMixerComp*     soundMixer = ecs_view_write_t(globalItr, SndMixerComp);

  UiGlobalResourcesComp* globalResources = ui_global_resources(world);
  if (!globalResources) {
    globalResources = ecs_world_add_t(world, ecs_world_global(world), UiGlobalResourcesComp);
    for (UiAtlasRes res = 0; res != UiAtlasRes_Count; ++res) {
      globalResources->atlases[res] = asset_lookup(world, assets, g_uiAtlasIds[res]);
    }
    for (UiGraphicRes res = 0; res != UiGraphicRes_Count; ++res) {
      globalResources->graphics[res] = asset_lookup(world, assets, g_uiGraphicIds[res]);
    }
    for (UiSoundRes res = 0; res != UiSoundRes_Count; ++res) {
      globalResources->sounds[res] = asset_lookup(world, assets, g_uiSoundIds[res]);
      snd_mixer_persistent_asset(soundMixer, globalResources->sounds[res]);
    }
  }

  for (UiAtlasRes res = 0; res != UiAtlasRes_Count; ++res) {
    const EcsEntityId atlas       = globalResources->atlases[res];
    const bool        isAcquired  = (globalResources->acquiredAtlases & (1 << res)) != 0;
    const bool        isUnloading = (globalResources->unloadingAtlases & (1 << res)) != 0;
    const bool        isLoaded    = ecs_world_has_t(world, atlas, AssetLoadedComp);
    const bool        isFailed    = ecs_world_has_t(world, atlas, AssetFailedComp);
    const bool        hasChanged  = ecs_world_has_t(world, atlas, AssetChangedComp);

    if (isFailed) {
      log_e(
          "Failed to load ui {} atlas",
          log_param("type", fmt_text(g_uiAtlasResNames[res])),
          log_param("id", fmt_text(g_uiAtlasIds[res])));
    }
    if (!isAcquired && !isUnloading) {
      log_i(
          "Acquiring ui {} atlas",
          log_param("type", fmt_text(g_uiAtlasResNames[res])),
          log_param("id", fmt_text(g_uiAtlasIds[res])));
      asset_acquire(world, globalResources->atlases[res]);
      globalResources->acquiredAtlases |= 1 << res;
    }
    if (isAcquired && (isLoaded || isFailed) && hasChanged) {
      asset_release(world, atlas);
      globalResources->acquiredAtlases &= ~(1 << res);
      globalResources->unloadingAtlases |= 1 << res;
    }
    if (isUnloading && !(isLoaded || isFailed)) {
      globalResources->unloadingAtlases &= ~(1 << res); // Unload finished.
    }
  }
}

ecs_module_init(ui_resource_module) {
  ecs_register_comp(UiGlobalResourcesComp);

  ecs_register_view(GlobalInitView);
  ecs_register_view(GlobalResourcesView);

  ecs_register_system(
      UiResourceUpdateSys, ecs_view_id(GlobalInitView), ecs_view_id(GlobalResourcesView));
}

EcsEntityId ui_resource_atlas(const UiGlobalResourcesComp* comp, const UiAtlasRes res) {
  diag_assert(res < UiAtlasRes_Count);
  return comp->atlases[res];
}

EcsEntityId ui_resource_graphic(const UiGlobalResourcesComp* comp, const UiGraphicRes res) {
  diag_assert(res < UiGraphicRes_Count);
  return comp->graphics[res];
}

EcsEntityId ui_resource_sound(const UiGlobalResourcesComp* comp, const UiSoundRes res) {
  diag_assert(res < UiSoundRes_Count);
  return comp->sounds[res];
}
