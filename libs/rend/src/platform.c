#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"

#include "platform_internal.h"
#include "rvk/device_internal.h"

ecs_comp_define_public(RendPlatformComp);

static void ecs_destruct_platform_comp(void* data) {
  MAYBE_UNUSED RendPlatformComp* comp = data;
  diag_assert_msg(!comp->device, "Render device not torn down, call rend_teardown() before exit");
}

ecs_view_define(GlobalView) { ecs_access_write(RendPlatformComp); }

ecs_system_define(RendPlatformUpdateSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), RendPlatformComp)) {
    ecs_world_add_t(
        world,
        ecs_world_global(world),
        RendPlatformComp,
        .device = rvk_device_create(g_alloc_heap));
    return;
  }

  EcsView*          globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator*      globalItr  = ecs_view_at(globalView, ecs_world_global(world));
  RendPlatformComp* plat       = ecs_view_write_t(globalItr, RendPlatformComp);

  rvk_device_update(plat->device);
}

ecs_module_init(rend_platform_module) {
  ecs_register_comp(RendPlatformComp, .destructor = ecs_destruct_platform_comp);

  ecs_register_view(GlobalView);

  ecs_register_system(RendPlatformUpdateSys, ecs_view_id(GlobalView));
}

void rend_platform_teardown(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_at(globalView, ecs_world_global(world));

  RendPlatformComp* plat = ecs_view_write_t(globalItr, RendPlatformComp);
  rvk_device_destroy(plat->device);
  plat->device = null;
}
