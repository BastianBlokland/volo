#include "core/alloc.h"
#include "ecs/world.h"
#include "loc/manager.h"

ecs_comp_define(LocManagerComp) { String preferredLocale; };

static void ecs_destruct_loc_manager(void* data) {
  LocManagerComp* comp = data;
  string_maybe_free(g_allocHeap, comp->preferredLocale);
}

ecs_module_init(loc_manager_module) {
  ecs_register_comp(LocManagerComp, .destructor = ecs_destruct_loc_manager);
}

LocManagerComp* loc_manager_init(EcsWorld* world, const String preferredLocale) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      LocManagerComp,
      .preferredLocale = string_maybe_dup(g_allocHeap, preferredLocale));
}
