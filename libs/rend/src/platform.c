#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "platform_internal.h"

ecs_comp_define_public(RendPlatformComp);

static void ecs_destruct_platform_comp(void* data) {
  MAYBE_UNUSED RendPlatformComp* comp = data;
  diag_assert_msg(!comp->vulkan, "Render platform not torn down, call rend_teardown() before exit");
}

ecs_view_define(RendPlatformAnyView) { ecs_access_with(RendPlatformComp); };
ecs_view_define(RendPlatformView) { ecs_access_write(RendPlatformComp); };

ecs_system_define(RendPlatformCreateSys) {
  if (ecs_utils_any(world, RendPlatformAnyView)) {
    return;
  }

  ecs_world_add_t(
      world,
      ecs_world_entity_create(world),
      RendPlatformComp,
      .vulkan = rvk_platform_create(g_alloc_heap));
}

ecs_module_init(rend_platform_module) {
  ecs_register_comp(RendPlatformComp, .destructor = ecs_destruct_platform_comp);

  ecs_register_view(RendPlatformAnyView);
  ecs_register_view(RendPlatformView);

  ecs_register_system(RendPlatformCreateSys, ecs_view_id(RendPlatformAnyView));
}

void rend_platform_teardown(EcsWorld* world) {
  RendPlatformComp* plat = ecs_utils_write_first_t(world, RendPlatformView, RendPlatformComp);
  rvk_platform_destroy(plat->vulkan);
  plat->vulkan = null;
}
