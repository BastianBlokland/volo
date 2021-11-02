#include "core_alloc.h"
#include "ecs_world.h"

#include "platform_internal.h"

ecs_comp_define_public(RendPlatformComp);

static void ecs_destruct_platform_comp(void* data) {
  RendPlatformComp* comp = data;
  rend_vk_platform_destroy(comp->vulkan);
}

ecs_view_define(RendPlatformView) { ecs_access_write(RendPlatformComp); };

static RendPlatformComp* rend_platform_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, RendPlatformView);
  EcsIterator* itr  = ecs_view_itr_first(view);
  if (itr) {
    return ecs_view_write_t(itr, RendPlatformComp);
  }
  return ecs_world_add_t(
      world,
      ecs_world_entity_create(world),
      RendPlatformComp,
      .vulkan = rend_vk_platform_create(g_alloc_heap));
}

ecs_system_define(RendPlatformUpdateSys) {
  RendPlatformComp* platform = rend_platform_get_or_create(world);
  (void)platform;
}

ecs_module_init(rend_platform_module) {
  ecs_register_comp(RendPlatformComp, .destructor = ecs_destruct_platform_comp);

  ecs_register_view(RendPlatformView);

  ecs_register_system(RendPlatformUpdateSys, ecs_view_id(RendPlatformView));
}
