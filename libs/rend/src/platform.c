#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_register.h"
#include "rend_settings.h"

#include "builder_internal.h"
#include "platform_internal.h"
#include "reset_internal.h"
#include "rvk/device_internal.h"
#include "rvk/pass_internal.h"

// clang-format off

static const RvkPassConfig g_passConfig[AssetGraphicPass_Count] = {
    [AssetGraphicPass_Geometry] = { .id = AssetGraphicPass_Geometry, .name = string_static("Geometry"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Stored,
        .attachDepthLoad = RvkPassLoad_Clear,

        // Attachment color 0: color (rgb) and emissive (a).
        .attachColorFormat[0] = RvkPassFormat_Color4Srgb,
        .attachColorLoad[0]   = RvkPassLoad_DontCare,

        // Attachment color 1: normal (rg), roughness (b) and tags (a).
        .attachColorFormat[1] = RvkPassFormat_Color4Linear,
        .attachColorLoad[1]   = RvkPassLoad_DontCare,
    },

    [AssetGraphicPass_Decal] = { .id = AssetGraphicPass_Decal, .name = string_static("Decal"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Stored,
        .attachDepthLoad = RvkPassLoad_Preserve,

        // Attachment color 0: color (rgb) and emissive (a).
        .attachColorFormat[0] = RvkPassFormat_Color4Srgb,
        .attachColorLoad[0]   = RvkPassLoad_Preserve,

        // Attachment color 1: normal (rg), roughness (b) and tags (a).
        .attachColorFormat[1] = RvkPassFormat_Color4Linear,
        .attachColorLoad[1]   = RvkPassLoad_Preserve,
    },

    [AssetGraphicPass_Fog] = { .id = AssetGraphicPass_Fog, .name = string_static("Fog"),
        // Attachment color 0: vision (r).
        .attachColorFormat[0] = RvkPassFormat_Color1Linear,
        .attachColorLoad[0]   = RvkPassLoad_Clear,
    },

    [AssetGraphicPass_FogBlur] = { .id = AssetGraphicPass_FogBlur, .name = string_static("FogBlur"),
        // Attachment color 0: vision (r).
        .attachColorFormat[0] = RvkPassFormat_Color1Linear,
        .attachColorLoad[0]   = RvkPassLoad_PreserveDontCheck,
    },

    [AssetGraphicPass_Shadow] = { .id = AssetGraphicPass_Shadow, .name = string_static("Shadow"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Stored,
        .attachDepthLoad = RvkPassLoad_Clear,
    },

    [AssetGraphicPass_AmbientOcclusion] = { .id = AssetGraphicPass_AmbientOcclusion, .name = string_static("AmbientOcclusion"),
        // Attachment color 0: occlusion (r).
        .attachColorFormat[0] = RvkPassFormat_Color1Linear,
        .attachColorLoad[0]   = RvkPassLoad_DontCare,
    },

    [AssetGraphicPass_Forward] = { .id = AssetGraphicPass_Forward, .name = string_static("Forward"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Stored, // Stored as Distortion still needs the depth.
        .attachDepthLoad = RvkPassLoad_Preserve,

        // Attachment color 0: color (rgb).
        .attachColorFormat[0] = RvkPassFormat_Color3Float,
        .attachColorLoad[0]   = RvkPassLoad_DontCare,
    },

    [AssetGraphicPass_Distortion] = { .id = AssetGraphicPass_Distortion, .name = string_static("Distortion"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Transient,
        .attachDepthLoad = RvkPassLoad_Preserve,

        // Attachment color 0: distortion-offset(rg).
        .attachColorFormat[0] = RvkPassFormat_Color2SignedFloat,
        .attachColorLoad[0]   = RvkPassLoad_Clear,
    },

    [AssetGraphicPass_Bloom] = { .id = AssetGraphicPass_Bloom, .name = string_static("Bloom"),
        // Attachment color 0: bloom (rgb).
        .attachColorFormat[0] = RvkPassFormat_Color3Float,
        .attachColorLoad[0]   = RvkPassLoad_PreserveDontCheck,
    },

    [AssetGraphicPass_Post] = { .id = AssetGraphicPass_Post, .name = string_static("Post"),
        // Attachment color 0: color (rgba).
        .attachColorFormat[0] = RvkPassFormat_Color4Srgb,
        .attachColorLoad[0]   = RvkPassLoad_DontCare,
    },
};

// clang-format on

ecs_comp_define_public(RendPlatformComp);
ecs_comp_define(RendPlatformInternComp) { RvkDevice* device; };

static void destruct_platform_comp(void* data) {
  RendPlatformComp* comp = data;
  log_d("Render platform teardown", log_param("phase", fmt_text_lit("Cleanup")));
  rend_builder_destroy(comp->builder);
  for (AssetGraphicPass i = 0; i != AssetGraphicPass_Count; ++i) {
    rvk_pass_destroy(comp->passes[i]);
  }
  rvk_device_destroy(comp->device);
}

static void destruct_platform_intern_comp(void* data) {
  RendPlatformComp* comp = data;
  /**
   * To aid the in proper teardown 'RendPlatformInternComp' is ordered to be destroyed before
   * any other render resources. This gives us a convenient place to wait for the gpu to be finished
   * before tearing anything down.
   */
  log_d("Render platform teardown", log_param("phase", fmt_text_lit("Wait for idle")));
  rvk_device_wait_idle(comp->device);
}

ecs_view_define(GlobalPlatformView) { ecs_access_write(RendPlatformComp); }
ecs_view_define(GlobalSettingsView) { ecs_access_read(RendSettingsGlobalComp); }

static const RendSettingsGlobalComp* rend_global_settings(EcsWorld* world) {
  const EcsEntityId global       = ecs_world_global(world);
  EcsView*          settingsView = ecs_world_view_t(world, GlobalSettingsView);
  EcsIterator*      settingsItr  = ecs_view_maybe_at(settingsView, global);
  if (!settingsItr) {
    RendSettingsGlobalComp* settings = ecs_world_add_t(world, global, RendSettingsGlobalComp);
    rend_settings_global_to_default(settings);
    return settings;
  }
  return ecs_view_read_t(settingsItr, RendSettingsGlobalComp);
}

ecs_system_define(RendPlatformUpdateSys) {
  if (rend_will_reset(world)) {
    return;
  }

  const EcsEntityId global       = ecs_world_global(world);
  EcsView*          platformView = ecs_world_view_t(world, GlobalPlatformView);
  EcsIterator*      platformItr  = ecs_view_maybe_at(platformView, global);

  if (!platformItr) {
    log_i("Setting up renderer");

    const RendSettingsGlobalComp* settings = rend_global_settings(world);
    RendPlatformComp*             plat     = ecs_world_add_t(world, global, RendPlatformComp);
    plat->device                           = rvk_device_create(settings);
    plat->builder                          = rend_builder_create(g_allocHeap);

    for (AssetGraphicPass i = 0; i != AssetGraphicPass_Count; ++i) {
      plat->passes[i] = rvk_pass_create(plat->device, &g_passConfig[i]);
    }

    ecs_world_add_t(world, global, RendPlatformInternComp, .device = plat->device);
    return;
  }

  RendPlatformComp* plat = ecs_view_write_t(platformItr, RendPlatformComp);
  rvk_device_update(plat->device);
}

ecs_module_init(rend_platform_module) {
  ecs_register_comp(RendPlatformComp, .destructor = destruct_platform_comp, .destructOrder = 10);
  ecs_register_comp(
      RendPlatformInternComp, .destructor = destruct_platform_intern_comp, .destructOrder = -10);

  ecs_register_view(GlobalPlatformView);
  ecs_register_view(GlobalSettingsView);

  ecs_register_system(
      RendPlatformUpdateSys, ecs_view_id(GlobalPlatformView), ecs_view_id(GlobalSettingsView));
}

void rend_platform_teardown(EcsWorld* world) {
  ecs_utils_maybe_remove_t(world, ecs_world_global(world), RendPlatformComp);
  ecs_utils_maybe_remove_t(world, ecs_world_global(world), RendPlatformInternComp);
}
