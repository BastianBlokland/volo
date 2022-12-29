#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_register.h"
#include "rend_settings.h"

#include "platform_internal.h"
#include "reset_internal.h"
#include "rvk/device_internal.h"

ecs_comp_define_public(RendPlatformComp);
ecs_comp_define(RendPlatformInternComp) { RvkDevice* device; };

static void destruct_platform_comp(void* data) {
  RendPlatformComp* comp = data;
  log_d("Render platform teardown", log_param("phase", fmt_text_lit("Destroying device")));
  rvk_device_destroy(comp->device);
}

static void destruct_platform_intern_comp(void* data) {
  RendPlatformComp* comp = data;
  /**
   * To aid the in proper teardown 'RendPlatformInternComp' is ordered to be destroyed before
   * any other render resources. This gives us a convenient place to wait for the gpu to be finished
   * before tearing anything down.
   */
  log_d("Render platform teardown", log_param("phase", fmt_text_lit("Waiting for device")));
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
    RvkDevice*                    device   = rvk_device_create(settings);
    ecs_world_add_t(world, global, RendPlatformComp, .device = device);
    ecs_world_add_t(world, global, RendPlatformInternComp, .device = device);
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

  /**
   * Update the platform after all draws have been submitted.
   */
  ecs_order(RendPlatformUpdateSys, RendOrder_DrawExecute + 1);
}

void rend_platform_teardown(EcsWorld* world) {
  ecs_utils_maybe_remove_t(world, ecs_world_global(world), RendPlatformComp);
  ecs_utils_maybe_remove_t(world, ecs_world_global(world), RendPlatformInternComp);
}
