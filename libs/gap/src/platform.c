#include "ecs_world.h"

#include "platform_internal.h"

ecs_comp_define_public(GapPlatformComp);

static void ecs_destruct_platform_comp(void* data) {
  GapPlatformComp* comp = data;
  gap_pal_destroy(comp->pal);
}

ecs_view_define(GapPlatformView) { ecs_access_write(GapPlatformComp); }

static GapPlatformComp* gap_platform_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, GapPlatformView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  if (itr) {
    return ecs_view_write_t(itr, GapPlatformComp);
  }
  return ecs_world_add_t(
      world, ecs_world_global(world), GapPlatformComp, .pal = gap_pal_create(g_alloc_heap));
}

ecs_system_define(GapPlatformUpdateSys) {
  GapPlatformComp* platform = gap_platform_get_or_create(world);
  gap_pal_update(platform->pal);
}

ecs_module_init(gap_platform_module) {
  ecs_register_comp(GapPlatformComp, .destructor = ecs_destruct_platform_comp, .destructOrder = 20);

  ecs_register_view(GapPlatformView);

  /**
   * NOTE: Create the system with thread-affinity as some platforms use thread-local event-queues so
   * we need to serve them from the same thread every time.
   */
  ecs_register_system_with_flags(
      GapPlatformUpdateSys, EcsSystemFlags_ThreadAffinity, ecs_view_id(GapPlatformView));
}
