#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "platform_internal.h"

ecs_comp_define_public(RendPlatformComp);

static void ecs_destruct_platform_comp(void* data) {
  MAYBE_UNUSED RendPlatformComp* comp = data;
  diag_assert_msg(!comp->device, "Render device not torn down, call rend_teardown() before exit");
}

ecs_view_define(RendPlatformView) { ecs_access_write(RendPlatformComp); };

ecs_system_define(RendPlatformUpdateSys) {
  RendPlatformComp* plat = ecs_utils_write_first_t(world, RendPlatformView, RendPlatformComp);
  if (!plat) {
    plat = ecs_world_add_t(
        world,
        ecs_world_entity_create(world),
        RendPlatformComp,
        .device = rvk_device_create(g_alloc_heap));
  }

  rvk_device_update(plat->device);
}

ecs_module_init(rend_platform_module) {
  ecs_register_comp(RendPlatformComp, .destructor = ecs_destruct_platform_comp);

  ecs_register_view(RendPlatformView);

  ecs_register_system(RendPlatformUpdateSys, ecs_view_id(RendPlatformView));
}

void rend_platform_teardown(EcsWorld* world) {
  RendPlatformComp* plat = ecs_utils_write_first_t(world, RendPlatformView, RendPlatformComp);
  rvk_device_destroy(plat->device);
  plat->device = null;
}
