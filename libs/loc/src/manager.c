#include "asset/manager.h"
#include "core/alloc.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "loc/manager.h"

ecs_comp_define(LocManagerComp) { String preferredLocale; };

static void ecs_destruct_loc_manager(void* data) {
  LocManagerComp* comp = data;
  string_maybe_free(g_allocHeap, comp->preferredLocale);
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(LocManagerComp);
}

ecs_system_define(LocUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
}

ecs_module_init(loc_manager_module) {
  ecs_register_comp(LocManagerComp, .destructor = ecs_destruct_loc_manager);

  ecs_register_view(UpdateGlobalView);

  ecs_register_system(LocUpdateSys, ecs_view_id(UpdateGlobalView));
}

LocManagerComp* loc_manager_init(EcsWorld* world, const String preferredLocale) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      LocManagerComp,
      .preferredLocale = string_maybe_dup(g_allocHeap, preferredLocale));
}
