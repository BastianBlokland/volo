#include "core_alloc.h"
#include "core_bits.h"
#include "core_dynarray.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "input_manager.h"

#include "resource_internal.h"

ecs_comp_define(InputManagerComp) {
  EcsEntityId activeWindow;
  DynArray    triggeredActions; // u32[], name hashes of the triggered actions. Not sorted.
};

static void ecs_destruct_input_manager(void* data) {
  InputManagerComp* comp = data;
  dynarray_destroy(&comp->triggeredActions);
}

ecs_view_define(GlobalView) {
  ecs_access_read(InputResourcesComp);
  ecs_access_maybe_write(InputManagerComp);
}

ecs_view_define(WindowView) { ecs_access_read(GapWindowComp); }

static InputManagerComp* input_manager_create(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      InputManagerComp,
      .triggeredActions = dynarray_create_t(g_alloc_heap, u32, 8));
}

static void input_active_window_refresh(EcsWorld* world, InputManagerComp* manager) {
  if (manager->activeWindow && !ecs_world_exists(world, manager->activeWindow)) {
    manager->activeWindow = 0;
  }
  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, WindowView)); ecs_view_walk(itr);) {
    const GapWindowComp* window = ecs_view_read_t(itr, GapWindowComp);
    if (!manager->activeWindow || gap_window_events(window) & GapWindowEvents_FocusGained) {
      manager->activeWindow = ecs_view_entity(itr);
    }
  }
}

ecs_system_define(InputUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const InputResourcesComp* resources = ecs_view_read_t(globalItr, InputResourcesComp);
  InputManagerComp*         manager   = ecs_view_write_t(globalItr, InputManagerComp);
  if (!manager) {
    manager = input_manager_create(world);
  }
  input_active_window_refresh(world, manager);

  (void)resources;
}

ecs_module_init(input_manager_module) {
  ecs_register_comp(InputManagerComp, .destructor = ecs_destruct_input_manager);

  ecs_register_view(GlobalView);
  ecs_register_view(WindowView);

  ecs_register_system(InputUpdateSys, ecs_view_id(GlobalView), ecs_view_id(WindowView));
}

bool input_triggered(const InputManagerComp* manager, const String action) {
  return input_triggered_hash(manager, bits_hash_32(action));
}

bool input_triggered_hash(const InputManagerComp* manager, const u32 actionHash) {
  dynarray_for_t(&manager->triggeredActions, u32, triggeredActionHash) {
    if (*triggeredActionHash == actionHash) {
      return true;
    }
  }
  return false;
}
