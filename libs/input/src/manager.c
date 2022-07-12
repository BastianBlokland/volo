#include "asset_inputmap.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "input_manager.h"
#include "input_register.h"

#include "resource_internal.h"

ecs_comp_define(InputManagerComp) {
  EcsEntityId     activeWindow;
  InputBlocker    blockers;
  InputCursorMode cursorMode;
  f32             cursorPosNorm[2];
  f32             cursorDeltaNorm[2];
  f32             cursorAspect; // Aspect ratio of the window that currently contains the cursor.
  DynArray        triggeredActions; // u32[], name hashes of the triggered actions. Not sorted.
};

static void ecs_destruct_input_manager(void* data) {
  InputManagerComp* comp = data;
  dynarray_destroy(&comp->triggeredActions);
}

ecs_view_define(GlobalView) {
  ecs_access_read(InputResourcesComp);
  ecs_access_maybe_write(InputManagerComp);
}

ecs_view_define(WindowView) { ecs_access_write(GapWindowComp); }

ecs_view_define(InputMapView) { ecs_access_read(AssetInputMapComp); }

static InputManagerComp* input_manager_create(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      InputManagerComp,
      .triggeredActions = dynarray_create_t(g_alloc_heap, u32, 8));
}

static const AssetInputMapComp* input_global_map(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(world, InputMapView), entity);
  return itr ? ecs_view_read_t(itr, AssetInputMapComp) : null;
}

static bool input_binding_satisfied(const AssetInputBinding* binding, const GapWindowComp* win) {
  switch (binding->type) {
  case AssetInputType_Pressed:
    return gap_window_key_pressed(win, binding->key);
  case AssetInputType_Released:
    return gap_window_key_released(win, binding->key);
  case AssetInputType_Down:
    return gap_window_key_down(win, binding->key);
  }
  diag_crash();
}

static bool input_action_satisfied(
    const AssetInputMapComp* map, const AssetInputAction* action, const GapWindowComp* win) {
  for (usize i = 0; i != action->bindingCount; ++i) {
    const AssetInputBinding* binding = &map->bindings[action->bindingIndex + i];
    if (input_binding_satisfied(binding, win)) {
      return true;
    }
  }
  return false;
}

static void input_refresh_active_window(EcsWorld* world, InputManagerComp* manager) {
  if (manager->activeWindow && !ecs_world_exists(world, manager->activeWindow)) {
    manager->activeWindow = 0;
  }
  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, WindowView)); ecs_view_walk(itr);) {
    GapWindowComp* win            = ecs_view_write_t(itr, GapWindowComp);
    const bool     isActiveWindow = manager->activeWindow == ecs_view_entity(itr);
    if (!manager->activeWindow && gap_window_events(win) & GapWindowEvents_Focussed) {
      manager->activeWindow = ecs_view_entity(itr);
    } else if (gap_window_events(win) & GapWindowEvents_FocusGained) {
      manager->activeWindow = ecs_view_entity(itr);
    } else if (isActiveWindow && gap_window_events(win) & GapWindowEvents_FocusLost) {
      manager->activeWindow = 0;
      gap_window_flags_unset(win, GapWindowFlags_CursorLock | GapWindowFlags_CursorHide);
    }
  }
}

static void input_update_cursor(InputManagerComp* manager, GapWindowComp* win) {
  const GapVector pos     = gap_window_param(win, GapParam_CursorPos);
  const GapVector delta   = gap_window_param(win, GapParam_CursorDelta);
  const GapVector winSize = gap_window_param(win, GapParam_WindowSize);

  manager->cursorPosNorm[0]   = pos.x / (f32)winSize.x;
  manager->cursorPosNorm[1]   = pos.y / (f32)winSize.y;
  manager->cursorDeltaNorm[0] = delta.x / (f32)winSize.x;
  manager->cursorDeltaNorm[1] = delta.y / (f32)winSize.y;
  manager->cursorAspect       = (f32)winSize.width / (f32)winSize.height;

  switch (manager->cursorMode) {
  case InputCursorMode_Normal:
    gap_window_flags_unset(win, GapWindowFlags_CursorLock | GapWindowFlags_CursorHide);
    break;
  case InputCursorMode_Locked:
    gap_window_flags_set(win, GapWindowFlags_CursorLock | GapWindowFlags_CursorHide);
    break;
  }
}

static void input_update_triggered(
    InputManagerComp* manager, const AssetInputMapComp* map, GapWindowComp* win) {

  for (usize i = 0; i != map->actionCount; ++i) {
    const AssetInputAction* action = &map->actions[i];
    if (manager->blockers & action->blockerBits) {
      continue;
    }
    if (input_action_satisfied(map, action, win)) {
      *dynarray_push_t(&manager->triggeredActions, u32) = action->nameHash;
    }
  }
}

ecs_system_define(InputUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  InputManagerComp* manager = ecs_view_write_t(globalItr, InputManagerComp);
  if (!manager) {
    manager = input_manager_create(world);
  }
  // Clear the previous tick's data.
  manager->cursorDeltaNorm[0] = 0;
  manager->cursorDeltaNorm[1] = 0;
  dynarray_clear(&manager->triggeredActions);

  const InputResourcesComp* resources = ecs_view_read_t(globalItr, InputResourcesComp);
  const AssetInputMapComp*  map       = input_global_map(world, input_resource_map(resources));
  if (!map) {
    return; // Inputmap not loaded yet.
  }

  input_refresh_active_window(world, manager);
  if (!manager->activeWindow) {
    return; // No window currently active.
  }
  GapWindowComp* win = ecs_utils_write_t(world, WindowView, manager->activeWindow, GapWindowComp);

  input_update_cursor(manager, win);
  input_update_triggered(manager, map, win);
}

ecs_module_init(input_manager_module) {
  ecs_register_comp(InputManagerComp, .destructor = ecs_destruct_input_manager);

  ecs_register_view(GlobalView);
  ecs_register_view(WindowView);
  ecs_register_view(InputMapView);

  ecs_register_system(
      InputUpdateSys, ecs_view_id(GlobalView), ecs_view_id(WindowView), ecs_view_id(InputMapView));

  ecs_order(InputUpdateSys, InputOrder_Read);
}

EcsEntityId input_active_window(const InputManagerComp* manager) { return manager->activeWindow; }

void input_blocker_update(InputManagerComp* manager, const InputBlocker blocker, const bool value) {
  if (value) {
    manager->blockers |= blocker;
  } else {
    manager->blockers &= ~blocker;
  }
}

InputCursorMode input_cursor_mode(const InputManagerComp* manager) { return manager->cursorMode; }

void input_cursor_mode_set(InputManagerComp* manager, const InputCursorMode newMode) {
  manager->cursorMode = newMode;

  switch (newMode) {
  case InputCursorMode_Normal:
    manager->blockers &= ~InputBlocker_CursorLocked;
    break;
  case InputCursorMode_Locked:
    manager->blockers |= InputBlocker_CursorLocked;
    break;
  }
}

f32 input_cursor_x(const InputManagerComp* manager) { return manager->cursorPosNorm[0]; }
f32 input_cursor_y(const InputManagerComp* manager) { return manager->cursorPosNorm[1]; }
f32 input_cursor_delta_x(const InputManagerComp* manager) { return manager->cursorDeltaNorm[0]; }
f32 input_cursor_delta_y(const InputManagerComp* manager) { return manager->cursorDeltaNorm[1]; }
f32 input_cursor_aspect(const InputManagerComp* manager) { return manager->cursorAspect; }

bool input_triggered_hash(const InputManagerComp* manager, const u32 actionHash) {
  dynarray_for_t(&manager->triggeredActions, u32, triggeredActionHash) {
    if (*triggeredActionHash == actionHash) {
      return true;
    }
  }
  return false;
}
