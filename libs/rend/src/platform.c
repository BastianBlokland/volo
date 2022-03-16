#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

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

ecs_view_define(GlobalView) {
  ecs_access_write(RendPlatformComp);
  ecs_access_write(RendPlatformInternComp);
}

ecs_system_define(RendPlatformUpdateSys) {
  if (rend_will_reset(world)) {
    return;
  }

  if (!ecs_world_has_t(world, ecs_world_global(world), RendPlatformComp)) {
    RvkDevice* device = rvk_device_create(g_alloc_heap);
    ecs_world_add_t(world, ecs_world_global(world), RendPlatformComp, .device = device);
    ecs_world_add_t(world, ecs_world_global(world), RendPlatformInternComp, .device = device);
    return;
  }

  EcsView*          globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator*      globalItr  = ecs_view_at(globalView, ecs_world_global(world));
  RendPlatformComp* plat       = ecs_view_write_t(globalItr, RendPlatformComp);

  rvk_device_update(plat->device);
}

ecs_module_init(rend_platform_module) {
  ecs_register_comp(RendPlatformComp, .destructor = destruct_platform_comp, .destructOrder = 10);
  ecs_register_comp(
      RendPlatformInternComp, .destructor = destruct_platform_intern_comp, .destructOrder = -10);

  ecs_register_view(GlobalView);

  ecs_register_system(RendPlatformUpdateSys, ecs_view_id(GlobalView));
}

void rend_platform_teardown(EcsWorld* world) {
  ecs_utils_maybe_remove_t(world, ecs_world_global(world), RendPlatformComp);
  ecs_utils_maybe_remove_t(world, ecs_world_global(world), RendPlatformInternComp);
}
