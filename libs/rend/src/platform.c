#include "core_alloc.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "platform_internal.h"

ecs_comp_define_public(RendPlatformComp);

static void ecs_destruct_platform_comp(void* data) {
  RendPlatformComp* comp = data;
  rvk_platform_destroy(comp->vulkan);
}

ecs_view_define(RendPlatformAnyView) { ecs_access_with(RendPlatformComp); };

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

  ecs_register_system(RendPlatformCreateSys, ecs_view_id(RendPlatformAnyView));
}
