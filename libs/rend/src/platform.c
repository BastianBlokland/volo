#include "core_alloc.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_settings.h"

#include "builder_internal.h"
#include "platform_internal.h"
#include "reset_internal.h"
#include "rvk/device_internal.h"
#include "rvk/lib_internal.h"
#include "rvk/pass_internal.h"

// clang-format off

#define REND_DEFINE_PASS(_NAME_) \
    [AssetGraphicPass_##_NAME_] = { .id = AssetGraphicPass_##_NAME_, .name = string_static(#_NAME_),

static const RvkPassConfig g_passConfig[AssetGraphicPass_Count] = {

  REND_DEFINE_PASS(Geometry)
    // Attachment depth.
    .attachDepth     = RvkPassDepth_Stored,
    .attachDepthLoad = RvkPassLoad_Clear,

    // Attachment color 0: color (rgb) and tags (a).
    .attachColorFormat[0] = RvkPassFormat_Color4Srgb,
    .attachColorLoad[0]   = RvkPassLoad_DontCare,

    // Attachment color 1: normal (rg), roughness (b).
    .attachColorFormat[1] = RvkPassFormat_Color4Linear,
    .attachColorLoad[1]   = RvkPassLoad_DontCare,

    // Attachment color 2: emissive (rgb).
    .attachColorFormat[2] = RvkPassFormat_Color3LowPrecision,
    .attachColorLoad[2]   = RvkPassLoad_DontCare,
  },

  REND_DEFINE_PASS(Decal)
    // Attachment depth.
    .attachDepth     = RvkPassDepth_Stored,
    .attachDepthLoad = RvkPassLoad_Preserve,

    // Attachment color 0: color (rgb) and tags (a).
    .attachColorFormat[0] = RvkPassFormat_Color4Srgb,
    .attachColorLoad[0]   = RvkPassLoad_Preserve,

    // Attachment color 1: normal (rg), roughness (b).
    .attachColorFormat[1] = RvkPassFormat_Color4Linear,
    .attachColorLoad[1]   = RvkPassLoad_Preserve,
  },

  REND_DEFINE_PASS(Fog)
    // Attachment color 0: vision (r).
    .attachColorFormat[0] = RvkPassFormat_Color1Linear,
    .attachColorLoad[0]   = RvkPassLoad_Clear,
  },

  REND_DEFINE_PASS(FogBlur)
    // Attachment color 0: vision (r).
    .attachColorFormat[0] = RvkPassFormat_Color1Linear,
    .attachColorLoad[0]   = RvkPassLoad_PreserveDontCheck,
  },

  REND_DEFINE_PASS(Shadow)
    // Attachment depth.
    .attachDepth     = RvkPassDepth_Stored,
    .attachDepthLoad = RvkPassLoad_Clear,
  },

  REND_DEFINE_PASS(AmbientOcclusion)
    // Attachment color 0: occlusion (r).
    .attachColorFormat[0] = RvkPassFormat_Color1Linear,
    .attachColorLoad[0]   = RvkPassLoad_DontCare,
  },

  REND_DEFINE_PASS(Forward)
    // Attachment depth.
    .attachDepth     = RvkPassDepth_Stored, // Stored as Distortion still needs the depth.
    .attachDepthLoad = RvkPassLoad_Preserve,

    // Attachment color 0: color (rgb).
    .attachColorFormat[0] = RvkPassFormat_Color3Float,
    .attachColorLoad[0]   = RvkPassLoad_DontCare,
  },

  REND_DEFINE_PASS(Distortion)
    // Attachment depth.
    .attachDepth     = RvkPassDepth_Transient,
    .attachDepthLoad = RvkPassLoad_Preserve,

    // Attachment color 0: distortion-offset(rg).
    .attachColorFormat[0] = RvkPassFormat_Color2SignedFloat,
    .attachColorLoad[0]   = RvkPassLoad_Clear,
  },

  REND_DEFINE_PASS(Bloom)
    // Attachment color 0: bloom (rgb).
    .attachColorFormat[0] = RvkPassFormat_Color3Float,
    .attachColorLoad[0]   = RvkPassLoad_PreserveDontCheck,
  },

  REND_DEFINE_PASS(Post)
    // Attachment color 0: color (rgba).
    .attachColorFormat[0] = RvkPassFormat_Swapchain,
    .attachColorLoad[0]   = RvkPassLoad_DontCare,
  },
};

#undef REND_DEFINE_PASS

// clang-format on

ecs_comp_define_public(RendPlatformComp);
ecs_comp_define(RendPlatformInternComp) { RvkDevice* device; };

static void destruct_platform_comp(void* data) {
  RendPlatformComp* comp = data;
  log_d("Render platform teardown", log_param("phase", fmt_text_lit("Cleanup")));
  rend_builder_container_destroy(comp->builderContainer);
  for (AssetGraphicPass i = 0; i != AssetGraphicPass_Count; ++i) {
    rvk_pass_destroy(comp->passes[i]);
  }
  rvk_device_destroy(comp->device);
  rvk_lib_destroy(comp->lib);
}

static void destruct_platform_intern_comp(void* data) {
  RendPlatformInternComp* comp = data;
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
    plat->lib                              = rvk_lib_create(settings);
    plat->device                           = rvk_device_create(plat->lib);
    plat->builderContainer                 = rend_builder_container_create(g_allocHeap);

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
