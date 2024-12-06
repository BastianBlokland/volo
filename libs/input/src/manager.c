#include "asset_inputmap.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "input_manager.h"
#include "input_register.h"

#include "resource_internal.h"

typedef struct {
  StringHash nameHash;
  GapKey     primarykey;
} InputActionInfo;

ecs_comp_define(InputManagerComp) {
  EcsEntityId     activeWindow;
  InputBlocker    blockers : 16;
  InputModifier   modifiers : 8;
  InputCursorMode cursorMode : 8;
  f32             cursorPosNorm[2];
  f32             cursorDeltaNorm[2];
  f32             cursorAspect; // Aspect ratio of the window that currently contains the cursor.
  f32             scrollDelta[2];
  TimeDuration    doubleclickInterval;
  DynArray        triggeredActions; // StringHash[], names of the triggered actions. Not sorted.
  DynArray        activeLayers;     // StringHash[], names of the active layers. Not sorted.
  DynArray        actionInfos;      // InputActionInfo[], sorted on the name.
};

static void ecs_destruct_input_manager(void* data) {
  InputManagerComp* comp = data;
  dynarray_destroy(&comp->triggeredActions);
  dynarray_destroy(&comp->activeLayers);
  dynarray_destroy(&comp->actionInfos);
}

static i8 input_compare_action_info(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, InputActionInfo, nameHash), field_ptr(b, InputActionInfo, nameHash));
}

ecs_view_define(GlobalView) {
  ecs_access_read(InputResourceComp);
  ecs_access_maybe_write(InputManagerComp);
}

ecs_view_define(WindowView) { ecs_access_write(GapWindowComp); }

ecs_view_define(InputMapView) { ecs_access_read(AssetInputMapComp); }

static InputManagerComp* input_manager_create(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      InputManagerComp,
      .triggeredActions = dynarray_create_t(g_allocHeap, StringHash, 8),
      .activeLayers     = dynarray_create_t(g_allocHeap, StringHash, 2),
      .actionInfos      = dynarray_create_t(g_allocHeap, InputActionInfo, 64));
}

static const AssetInputMapComp* input_map_asset(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(world, InputMapView), entity);
  return itr ? ecs_view_read_t(itr, AssetInputMapComp) : null;
}

static bool input_binding_satisfied(
    const InputManagerComp* manager, const AssetInputBinding* binding, const GapWindowComp* win) {

  // Check that all required modifiers are active.
  if ((binding->requiredModifierBits & manager->modifiers) != binding->requiredModifierBits) {
    return false;
  }

  // Check that none of the illegal modifiers are active.
  if (binding->illegalModifierBits & manager->modifiers) {
    return false;
  }

  // Check that the key is active.
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
    const InputManagerComp*  manager,
    const AssetInputMapComp* map,
    const AssetInputAction*  action,
    const GapWindowComp*     win) {

  for (usize i = 0; i != action->bindingCount; ++i) {
    const AssetInputBinding* binding = &map->bindings.values[action->bindingIndex + i];
    if (input_binding_satisfied(manager, binding, win)) {
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

static void input_update_blockers(InputManagerComp* manager, GapWindowComp* win) {
  if (gap_window_flags(win) & GapWindowFlags_CursorConfine) {
    manager->blockers |= InputBlocker_CursorConfined;
  } else {
    manager->blockers &= ~InputBlocker_CursorConfined;
  }
  if (gap_window_mode(win) == GapWindowMode_Fullscreen) {
    manager->blockers |= InputBlocker_WindowFullscreen;
  } else {
    manager->blockers &= ~InputBlocker_WindowFullscreen;
  }
}

static void input_update_modifiers(InputManagerComp* manager, GapWindowComp* win) {
  manager->modifiers = 0;
  if (gap_window_key_down(win, GapKey_Shift)) {
    manager->modifiers |= InputModifier_Shift;
  }
  if (gap_window_key_down(win, GapKey_Control)) {
    manager->modifiers |= InputModifier_Control;
  }
  if (gap_window_key_down(win, GapKey_Alt)) {
    manager->modifiers |= InputModifier_Alt;
  }
}

static void input_update_cursor(InputManagerComp* manager, GapWindowComp* win) {
  const GapVector pos     = gap_window_param(win, GapParam_CursorPos);
  const GapVector delta   = gap_window_param(win, GapParam_CursorDelta);
  const GapVector scroll  = gap_window_param(win, GapParam_ScrollDelta);
  const GapVector winSize = gap_window_param(win, GapParam_WindowSize);

  if (winSize.x > 0 && winSize.y > 0) {
    manager->cursorPosNorm[0]   = pos.x / (f32)winSize.x;
    manager->cursorPosNorm[1]   = pos.y / (f32)winSize.y;
    manager->cursorDeltaNorm[0] = delta.x / (f32)winSize.x;
    manager->cursorDeltaNorm[1] = delta.y / (f32)winSize.y;
    manager->cursorAspect       = (f32)winSize.width / (f32)winSize.height;
    manager->scrollDelta[0]     = scroll.x;
    manager->scrollDelta[1]     = scroll.y;
  } else {
    manager->cursorPosNorm[0] = 0.5f;
    manager->cursorPosNorm[1] = 0.5f;
    mem_set(array_mem(manager->cursorDeltaNorm), 0);
    manager->cursorAspect = 1.0f;
    mem_set(array_mem(manager->scrollDelta), 0);
  }

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

  for (usize i = 0; i != map->actions.count; ++i) {
    const AssetInputAction* action = &map->actions.values[i];
    if (manager->blockers & action->blockerBits) {
      continue;
    }
    if (input_action_satisfied(manager, map, action, win)) {
      *dynarray_push_t(&manager->triggeredActions, StringHash) = action->nameHash;
    }
  }
}

static void input_update_key_info(InputManagerComp* manager, const AssetInputMapComp* map) {
  for (usize i = 0; i != map->actions.count; ++i) {
    const AssetInputAction* action = &map->actions.values[i];
    if (UNLIKELY(!action->bindingCount)) {
      continue;
    }
    const AssetInputBinding* primaryBinding = &map->bindings.values[action->bindingIndex];
    const InputActionInfo info = {.nameHash = action->nameHash, .primarykey = primaryBinding->key};
    *dynarray_insert_sorted_t(
        &manager->actionInfos, InputActionInfo, input_compare_action_info, &info) = info;
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

  const InputResourceComp* resource = ecs_view_read_t(globalItr, InputResourceComp);

  input_refresh_active_window(world, manager);
  if (!manager->activeWindow) {
    return; // No window currently active.
  }
  GapWindowComp* win = ecs_utils_write_t(world, WindowView, manager->activeWindow, GapWindowComp);

  input_update_blockers(manager, win);
  input_update_modifiers(manager, win);
  input_update_cursor(manager, win);
  manager->doubleclickInterval = gap_window_doubleclick_interval(win);

  dynarray_clear(&manager->actionInfos);

  EcsEntityId mapAssets[input_resource_max_maps];
  u32         mapAssetCount = input_resource_maps(resource, mapAssets);
  for (u32 i = 0; i != mapAssetCount; ++i) {
    const AssetInputMapComp* map = input_map_asset(world, mapAssets[i]);
    if (map && input_layer_active(manager, map->layer)) {
      input_update_triggered(manager, map, win);
      input_update_key_info(manager, map);
    }
  }
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

InputBlocker input_blockers(const InputManagerComp* manager) { return manager->blockers; }

void input_blocker_update(InputManagerComp* manager, const InputBlocker blocker, const bool value) {
  if (value) {
    manager->blockers |= blocker;
  } else {
    manager->blockers &= ~blocker;
  }
}

InputModifier input_modifiers(const InputManagerComp* manager) { return manager->modifiers; }

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
f32 input_scroll_x(const InputManagerComp* manager) { return manager->scrollDelta[0]; }
f32 input_scroll_y(const InputManagerComp* manager) { return manager->scrollDelta[1]; }

TimeDuration input_doubleclick_interval(const InputManagerComp* manager) {
  return manager->doubleclickInterval;
}

bool input_triggered_hash(const InputManagerComp* manager, const StringHash actionHash) {
  InputManagerComp* manMut = (InputManagerComp*)manager;
  return dynarray_search_linear(&manMut->triggeredActions, compare_stringhash, &actionHash) != null;
}

GapKey input_primary_key(const InputManagerComp* manager, const StringHash actionHash) {
  InputManagerComp*      manMut = (InputManagerComp*)manager;
  const InputActionInfo* info   = dynarray_search_binary(
      &manMut->actionInfos, input_compare_action_info, &(InputActionInfo){.nameHash = actionHash});
  return info ? info->primarykey : GapKey_None;
}

void input_layer_enable(InputManagerComp* manager, const StringHash layerHash) {
  diag_assert(layerHash != 0);

  if (!dynarray_search_linear(&manager->activeLayers, compare_stringhash, &layerHash)) {
    *dynarray_push_t(&manager->activeLayers, StringHash) = layerHash;
  }
}

void input_layer_disable(InputManagerComp* manager, const StringHash layerHash) {
  diag_assert(layerHash != 0);

  StringHash* e = dynarray_search_linear(&manager->activeLayers, compare_stringhash, &layerHash);
  if (e) {
    const usize index = e - dynarray_begin_t(&manager->activeLayers, StringHash);
    dynarray_remove_unordered(&manager->activeLayers, index, 1);
  }
}

bool input_layer_active(const InputManagerComp* manager, const StringHash layerHash) {
  if (!layerHash) {
    return true; // The empty layer is always considered to be active.
  }
  InputManagerComp* manMut = (InputManagerComp*)manager;
  return dynarray_search_linear(&manMut->activeLayers, compare_stringhash, &layerHash) != null;
}
