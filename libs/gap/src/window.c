#include "core_path.h"
#include "core_sentinel.h"
#include "core_signal.h"
#include "core_thread.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_register.h"
#include "gap_window.h"

#include "platform_internal.h"

typedef enum {
  GapWindowRequests_Create           = 1 << 0,
  GapWindowRequests_Close            = 1 << 1,
  GapWindowRequests_Resize           = 1 << 2,
  GapWindowRequests_UpdateTitle      = 1 << 3,
  GapWindowRequests_UpdateCursorHide = 1 << 4,
  GapWindowRequests_UpdateCursorLock = 1 << 5,
} GapWindowRequests;

ecs_comp_define(GapWindowComp) {
  GapPal* pal;
  String  title;
  uptr    nativeAppHandle;

  GapWindowId       id;
  GapWindowEvents   events : 16;
  GapWindowFlags    flags : 8;
  GapWindowRequests requests : 8;
  GapWindowMode     mode : 8;
  GapKeySet         keysPressed, keysReleased, keysDown;
  GapVector         params[GapParam_Count];
  DynString         inputText;
};

static void ecs_destruct_window_comp(void* data) {
  GapWindowComp* comp = data;
  if (comp->pal) {
    gap_pal_window_destroy(comp->pal, comp->id);
  }
  if (!string_is_empty(comp->title)) {
    string_free(g_alloc_heap, comp->title);
  }
  dynstring_destroy(&comp->inputText);
}

static String window_default_title_scratch(const GapWindowComp* window) {
  return fmt_write_scratch(
      "{} (pid: {}, wid: {})",
      fmt_text(path_stem(g_path_executable)),
      fmt_int(g_thread_pid),
      fmt_int(window->id));
}

static bool window_should_close(GapWindowComp* win) {
  if (win->requests & GapWindowRequests_Close) {
    return true;
  }
  if (win->flags & GapWindowFlags_CloseOnInterupt && signal_is_received(Signal_Interupt)) {
    return true;
  }
  if (win->flags & GapWindowFlags_CloseOnRequest && win->events & GapWindowEvents_CloseRequested) {
    return true;
  }
  return false;
}

static void window_update(
    EcsWorld*         world,
    GapPlatformComp*  platform,
    GapWindowComp*    window,
    const EcsEntityId windowEntity) {

  window->pal             = platform->pal;
  window->nativeAppHandle = gap_pal_native_app_handle(platform->pal);

  // Clear the events of the previous tick.
  window->events = 0;

  if (window->requests & GapWindowRequests_Create) {
    window->id = gap_pal_window_create(platform->pal, window->params[GapParam_WindowSize]);
    if (window->flags & GapWindowFlags_DefaultTitle) {
      gap_window_title_set(window, window_default_title_scratch(window));
    }
  }
  if (window->requests & GapWindowRequests_UpdateTitle) {
    gap_pal_window_title_set(platform->pal, window->id, window->title);
    window->events |= GapWindowEvents_TitleUpdated;
  }
  if (window->requests & GapWindowRequests_Resize) {
    const bool fullscreen = window->mode == GapWindowMode_Fullscreen;
    gap_pal_window_resize(
        platform->pal, window->id, window->params[GapParam_WindowSize], fullscreen);
  }
  if (window->requests & GapWindowRequests_UpdateCursorHide) {
    const bool hidden = (window->flags & GapWindowFlags_CursorHide) != 0;
    gap_pal_window_cursor_hide(platform->pal, window->id, hidden);
  }
  if (window->requests & GapWindowRequests_UpdateCursorLock) {
    const bool locked = (window->flags & GapWindowFlags_CursorLock) != 0;
    /**
     * Capturing the cursor allows receiving mouse inputs even if the cursor is no longer over the
     * window. This is usefull as you can then do bigger sweeps without losing the lock.
     */
    gap_pal_window_cursor_capture(platform->pal, window->id, locked);
  }

  const GapPalWindowFlags palFlags = gap_pal_window_flags(platform->pal, window->id);
  if (palFlags & GapPalWindowFlags_CloseRequested) {
    window->events |= GapWindowEvents_CloseRequested;
  }
  if (palFlags & GapPalWindowFlags_Resized) {
    const GapVector size = gap_pal_window_param(platform->pal, window->id, GapParam_WindowSize);
    window->params[GapParam_WindowSize] = size;
    window->events |= GapWindowEvents_Resized;
  }
  if (palFlags & GapPalWindowFlags_CursorMoved) {
    const GapVector oldPos = window->params[GapParam_CursorPos];
    const GapVector newPos = gap_pal_window_param(platform->pal, window->id, GapParam_CursorPos);
    window->params[GapParam_CursorPos] = newPos;
    if (palFlags & GapPalWindowFlags_FocusGained) {
      // NOTE: When gaining focus use delta zero to avoid jumps due to cursor moved in background.
      window->params[GapParam_CursorDelta] = gap_vector(0, 0);
    } else {
      window->params[GapParam_CursorDelta] = gap_vector_sub(newPos, oldPos);
    }
  } else {
    window->params[GapParam_CursorDelta] = gap_vector(0, 0);
  }
  if (palFlags & GapPalWindowFlags_Scrolled) {
    const GapVector delta = gap_pal_window_param(platform->pal, window->id, GapParam_ScrollDelta);
    window->params[GapParam_ScrollDelta] = delta;
  } else {
    window->params[GapParam_ScrollDelta] = gap_vector(0, 0);
  }
  if (palFlags & GapPalWindowFlags_KeyPressed) {
    window->keysPressed = *gap_pal_window_keys_pressed(platform->pal, window->id);
    window->keysDown    = *gap_pal_window_keys_down(platform->pal, window->id);
    window->events |= GapWindowEvents_KeyPressed;
  } else {
    gap_keyset_clear(&window->keysPressed);
  }
  if (palFlags & GapPalWindowFlags_KeyReleased) {
    window->keysReleased = *gap_pal_window_keys_released(platform->pal, window->id);
    window->keysDown     = *gap_pal_window_keys_down(platform->pal, window->id);
    window->events |= GapWindowEvents_KeyReleased;
  } else {
    gap_keyset_clear(&window->keysReleased);
  }
  if (palFlags & GapPalWindowFlags_FocusGained) {
    window->events |= GapWindowEvents_FocusGained;
  }
  if (palFlags & GapPalWindowFlags_FocusLost) {
    window->events |= GapWindowEvents_FocusLost;
  }
  if (palFlags & GapPalWindowFlags_Focussed) {
    window->events |= GapWindowEvents_Focussed;
  }
  if (window->flags & GapWindowFlags_CursorLock) {
    const GapVector tgtPos = gap_vector_div(window->params[GapParam_WindowSize], 2);
    if (!gap_vector_equal(window->params[GapParam_CursorPos], tgtPos)) {
      gap_pal_window_cursor_set(platform->pal, window->id, tgtPos);
      window->params[GapParam_CursorPos] = tgtPos;
    }
  }
  dynstring_clear(&window->inputText);
  dynstring_append(&window->inputText, gap_pal_window_input_text(platform->pal, window->id));

  if (window_should_close(window)) {
    ecs_world_entity_destroy(world, windowEntity);
  }

  // All requests have been handled.
  window->requests = 0;
}

ecs_view_define(GapPlatformView) { ecs_access_write(GapPlatformComp); }
ecs_view_define(GapWindowView) { ecs_access_write(GapWindowComp); }

ecs_system_define(GapWindowUpdateSys) {
  GapPlatformComp* platform = ecs_utils_write_first_t(world, GapPlatformView, GapPlatformComp);
  if (!platform) {
    return;
  }

  EcsView* windowView = ecs_world_view_t(world, GapWindowView);
  for (EcsIterator* itr = ecs_view_itr(windowView); ecs_view_walk(itr);) {
    window_update(world, platform, ecs_view_write_t(itr, GapWindowComp), ecs_view_entity(itr));
  }
}

ecs_module_init(gap_window_module) {
  ecs_register_comp(GapWindowComp, .destructor = ecs_destruct_window_comp, .destructOrder = 20);

  ecs_register_view(GapPlatformView);
  ecs_register_view(GapWindowView);

  /**
   * NOTE: Create the system with thread-affinity as some platforms use thread-local event-queues so
   * we need to serve them from the same thread every time.
   */
  ecs_register_system_with_flags(
      GapWindowUpdateSys,
      EcsSystemFlags_ThreadAffinity,
      ecs_view_id(GapPlatformView),
      ecs_view_id(GapWindowView));
  ecs_order(GapWindowUpdateSys, GapOrder_WindowUpdate);
}

EcsEntityId gap_window_create(EcsWorld* world, const GapWindowFlags flags, const GapVector size) {
  const EcsEntityId windowEntity = ecs_world_entity_create(world);
  GapWindowComp*    comp         = ecs_world_add_t(
      world,
      windowEntity,
      GapWindowComp,
      .id                          = sentinel_u32,
      .events                      = GapWindowEvents_Initializing,
      .requests                    = GapWindowRequests_Create,
      .params[GapParam_WindowSize] = size,
      .inputText                   = dynstring_create(g_alloc_heap, 64));

  gap_window_flags_set(comp, flags);
  return windowEntity;
}

void gap_window_close(GapWindowComp* window) { window->requests |= GapWindowRequests_Close; }

GapWindowFlags gap_window_flags(const GapWindowComp* window) { return window->flags; }

void gap_window_flags_set(GapWindowComp* comp, const GapWindowFlags flags) {
  comp->flags |= flags;

  if (flags & GapWindowFlags_CursorHide) {
    comp->requests |= GapWindowRequests_UpdateCursorHide;
  }
  if (flags & GapWindowFlags_CursorLock) {
    comp->requests |= GapWindowRequests_UpdateCursorLock;
  }
}

void gap_window_flags_unset(GapWindowComp* comp, const GapWindowFlags flags) {
  comp->flags &= ~flags;

  if (flags & GapWindowFlags_CursorHide) {
    comp->requests |= GapWindowRequests_UpdateCursorHide;
  }
  if (flags & GapWindowFlags_CursorLock) {
    comp->requests |= GapWindowRequests_UpdateCursorLock;
  }
}

GapWindowEvents gap_window_events(const GapWindowComp* window) { return window->events; }

GapWindowMode gap_window_mode(const GapWindowComp* window) { return window->mode; }

void gap_window_resize(GapWindowComp* comp, const GapVector size, const GapWindowMode mode) {
  comp->params[GapParam_WindowSize] = size;
  comp->mode                        = mode;
  comp->requests |= GapWindowRequests_Resize;
}

String gap_window_title_get(const GapWindowComp* window) { return window->title; }

void gap_window_title_set(GapWindowComp* window, const String newTitle) {
  if (!string_is_empty(window->title)) {
    string_free(g_alloc_heap, window->title);
  }
  window->title = string_is_empty(newTitle) ? string_empty : string_dup(g_alloc_heap, newTitle);
  window->requests |= GapWindowRequests_UpdateTitle;
}

GapVector gap_window_param(const GapWindowComp* comp, const GapParam param) {
  return comp->params[param];
}

bool gap_window_key_pressed(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysPressed, key);
}

bool gap_window_key_released(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysReleased, key);
}

bool gap_window_key_down(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysDown, key);
}

String gap_window_input_text(const GapWindowComp* comp) { return dynstring_view(&comp->inputText); }

GapNativeWm gap_native_wm() { return gap_pal_native_wm(); }

uptr gap_native_window_handle(const GapWindowComp* comp) { return (uptr)comp->id; }

uptr gap_native_app_handle(const GapWindowComp* comp) { return comp->nativeAppHandle; }
