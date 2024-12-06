#include "core_dynstring.h"
#include "core_format.h"
#include "core_path.h"
#include "core_sentinel.h"
#include "core_signal.h"
#include "core_thread.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "gap_register.h"
#include "gap_window.h"

#include "platform_internal.h"

typedef enum {
  GapWindowRequests_Create              = 1 << 0,
  GapWindowRequests_Close               = 1 << 1,
  GapWindowRequests_Resize              = 1 << 2,
  GapWindowRequests_UpdateTitle         = 1 << 3,
  GapWindowRequests_UpdateIconType      = 1 << 4,
  GapWindowRequests_UpdateCursorHide    = 1 << 5,
  GapWindowRequests_UpdateCursorLock    = 1 << 6,
  GapWindowRequests_UpdateCursorConfine = 1 << 7,
  GapWindowRequests_UpdateCursorType    = 1 << 8,
  GapWindowRequests_ClipPaste           = 1 << 9,
} GapWindowRequests;

ecs_comp_define(GapWindowComp) {
  GapPal* pal;
  String  title;
  String  displayName;
  uptr    nativeAppHandle;

  GapWindowId       id;
  GapWindowEvents   events : 16;
  GapWindowFlags    flags : 8;
  GapWindowMode     mode : 8;
  GapWindowRequests requests : 16;
  GapKeySet         keysPressed, keysPressedWithRepeat, keysReleased, keysDown;
  GapVector         params[GapParam_Count];
  DynString         inputText;
  String            clipCopy, clipPaste;
  GapIcon           icon : 8;
  GapCursor         cursor : 8;
  u16               dpi;
  f32               refreshRate;
};

ecs_comp_define_public(GapWindowAspectComp);

static void ecs_destruct_window_comp(void* data) {
  GapWindowComp* comp = data;
  if (comp->pal) {
    gap_pal_window_destroy(comp->pal, comp->id);
  }
  string_maybe_free(g_allocHeap, comp->title);
  string_maybe_free(g_allocHeap, comp->displayName);
  dynstring_destroy(&comp->inputText);
  string_maybe_free(g_allocHeap, comp->clipCopy);
  string_maybe_free(g_allocHeap, comp->clipPaste);
}

static String window_default_title_scratch(const GapWindowComp* window) {
  return fmt_write_scratch(
      "{} (pid: {}, wid: {})",
      fmt_text(path_stem(g_pathExecutable)),
      fmt_int(g_threadPid),
      fmt_int(window->id));
}

static f32 window_aspect(const GapVector size) {
  if (!size.width || !size.height) {
    return 1.0f;
  }
  return (f32)size.width / (f32)size.height;
}

static bool window_should_close(GapWindowComp* win) {
  if (win->requests & GapWindowRequests_Close) {
    return true;
  }
  if (signal_is_received(Signal_Terminate)) {
    return true;
  }
  if (win->flags & GapWindowFlags_CloseOnInterrupt && signal_is_received(Signal_Interrupt)) {
    return true;
  }
  if (win->flags & GapWindowFlags_CloseOnRequest && win->events & GapWindowEvents_CloseRequested) {
    return true;
  }
  return false;
}

static void window_update(
    EcsWorld*            world,
    GapPlatformComp*     platform,
    GapWindowComp*       win,
    GapWindowAspectComp* winAspect,
    const EcsEntityId    winEntity) {

  GapPal* pal          = platform->pal;
  win->pal             = pal;
  win->nativeAppHandle = gap_pal_native_app_handle(pal);

  // Clear the events of the previous tick.
  win->events = 0;

  if (win->requests & GapWindowRequests_Create) {
    win->id          = gap_pal_window_create(pal, win->params[GapParam_WindowSize]);
    win->displayName = string_maybe_dup(g_allocHeap, gap_pal_window_display_name(pal, win->id));
    win->refreshRate = gap_pal_window_refresh_rate(pal, win->id);
    win->dpi         = gap_pal_window_dpi(pal, win->id);

    // Set the window-size to match the created window as additional constraints are applied.
    const GapVector createdWinSize   = gap_pal_window_param(pal, win->id, GapParam_WindowSize);
    win->params[GapParam_WindowSize] = createdWinSize;
    winAspect->ratio                 = window_aspect(createdWinSize);

    if (win->mode == GapWindowMode_Fullscreen) {
      win->params[GapParam_WindowSizePreFullscreen] = createdWinSize;
      gap_pal_window_resize(pal, win->id, createdWinSize, true);
    }
    const bool defaultTitle = (win->flags & GapWindowFlags_DefaultTitle) != 0;
    if (defaultTitle && (win->requests & GapWindowRequests_UpdateTitle) == 0) {
      gap_window_title_set(win, window_default_title_scratch(win));
    }
  }
  if (win->requests & GapWindowRequests_UpdateTitle) {
    gap_pal_window_title_set(pal, win->id, win->title);
    win->events |= GapWindowEvents_TitleUpdated;
  }
  if (win->requests & GapWindowRequests_Resize) {
    const bool fullscreen = win->mode == GapWindowMode_Fullscreen;
    gap_pal_window_resize(pal, win->id, win->params[GapParam_WindowSize], fullscreen);
  }
  if (win->requests & GapWindowRequests_UpdateIconType) {
    gap_pal_window_icon_set(pal, win->id, win->icon);
  }
  if (win->requests & GapWindowRequests_UpdateCursorHide) {
    const bool hidden = (win->flags & GapWindowFlags_CursorHide) != 0;
    gap_pal_window_cursor_hide(pal, win->id, hidden);
  }
  if (win->requests & GapWindowRequests_UpdateCursorLock) {
    const bool locked = (win->flags & GapWindowFlags_CursorLock) != 0;
    if (locked) {
      win->params[GapParam_CursorPosPreLock] = win->params[GapParam_CursorPos];
    } else {
      const GapVector preLockPos = win->params[GapParam_CursorPosPreLock];
      gap_pal_window_cursor_pos_set(pal, win->id, preLockPos);
      win->params[GapParam_CursorPos] = preLockPos;
    }
    /**
     * Capturing the cursor allows receiving mouse inputs even if the cursor is no longer over the
     * window. This is useful as you can then do bigger sweeps without losing the lock.
     */
    gap_pal_window_cursor_capture(pal, win->id, locked);
  }
  if (win->requests & GapWindowRequests_UpdateCursorConfine) {
    const bool confine = (win->flags & GapWindowFlags_CursorConfine) != 0;
    gap_pal_window_cursor_confine(pal, win->id, confine);
  }
  if (win->requests & GapWindowRequests_UpdateCursorType) {
    gap_pal_window_cursor_set(pal, win->id, win->cursor);
  }
  if (!string_is_empty(win->clipCopy)) {
    gap_pal_window_clip_copy(pal, win->id, win->clipCopy);
    string_free(g_allocHeap, win->clipCopy);
    win->clipCopy = string_empty;
  }
  if (win->requests & GapWindowRequests_ClipPaste) {
    gap_pal_window_clip_paste(pal, win->id);
  }

  const GapPalWindowFlags palFlags = gap_pal_window_flags(pal, win->id);
  if (palFlags & GapPalWindowFlags_CloseRequested) {
    win->events |= GapWindowEvents_CloseRequested;
  }
  if (palFlags & GapPalWindowFlags_Resized) {
    const GapVector size             = gap_pal_window_param(pal, win->id, GapParam_WindowSize);
    win->params[GapParam_WindowSize] = size;
    win->params[GapParam_CursorPos]  = gap_pal_window_param(pal, win->id, GapParam_CursorPos);
    win->events |= GapWindowEvents_Resized;
    winAspect->ratio = window_aspect(size);
  }
  if (palFlags & GapPalWindowFlags_CursorMoved) {
    const GapVector oldPos          = win->params[GapParam_CursorPos];
    const GapVector newPos          = gap_pal_window_param(pal, win->id, GapParam_CursorPos);
    win->params[GapParam_CursorPos] = newPos;
    if (palFlags & GapPalWindowFlags_FocusGained) {
      // NOTE: When gaining focus use delta zero to avoid jumps due to cursor moved in background.
      win->params[GapParam_CursorDelta] = gap_vector(0, 0);
    } else {
      win->params[GapParam_CursorDelta] = gap_vector_sub(newPos, oldPos);
    }
  } else {
    win->params[GapParam_CursorDelta] = gap_vector(0, 0);
  }
  if (palFlags & GapPalWindowFlags_Scrolled) {
    const GapVector delta             = gap_pal_window_param(pal, win->id, GapParam_ScrollDelta);
    win->params[GapParam_ScrollDelta] = delta;
  } else {
    win->params[GapParam_ScrollDelta] = gap_vector(0, 0);
  }
  if (palFlags & GapPalWindowFlags_KeyPressed) {
    win->keysPressed           = *gap_pal_window_keys_pressed(pal, win->id);
    win->keysPressedWithRepeat = *gap_pal_window_keys_pressed_with_repeat(pal, win->id);
    win->keysDown              = *gap_pal_window_keys_down(pal, win->id);
    win->events |= GapWindowEvents_KeyPressed;
  } else {
    gap_keyset_clear(&win->keysPressed);
    gap_keyset_clear(&win->keysPressedWithRepeat);
  }
  if (palFlags & GapPalWindowFlags_KeyReleased) {
    win->keysReleased = *gap_pal_window_keys_released(pal, win->id);
    win->keysDown     = *gap_pal_window_keys_down(pal, win->id);
    win->events |= GapWindowEvents_KeyReleased;
  } else {
    gap_keyset_clear(&win->keysReleased);
  }
  if (palFlags & GapPalWindowFlags_DisplayNameChanged) {
    string_maybe_free(g_allocHeap, win->displayName);
    win->displayName = string_maybe_dup(g_allocHeap, gap_pal_window_display_name(pal, win->id));
  }
  if (palFlags & GapPalWindowFlags_RefreshRateChanged) {
    win->refreshRate = gap_pal_window_refresh_rate(pal, win->id);
    win->events |= GapWindowEvents_RefreshRateChanged;
  }
  if (palFlags & GapPalWindowFlags_DpiChanged) {
    win->dpi = gap_pal_window_dpi(pal, win->id);
    win->events |= GapWindowEvents_DpiChanged;
  }
  if (palFlags & GapPalWindowFlags_FocusGained) {
    win->events |= GapWindowEvents_FocusGained;
  }
  if (palFlags & GapPalWindowFlags_FocusLost) {
    gap_keyset_clear(&win->keysDown);
    win->events |= GapWindowEvents_FocusLost;
  }
  if (palFlags & GapPalWindowFlags_Focussed) {
    win->events |= GapWindowEvents_Focussed;
  }
  if (win->flags & GapWindowFlags_CursorLock) {
    const GapVector tgtPos = gap_vector_div(win->params[GapParam_WindowSize], 2);
    if (!gap_vector_equal(win->params[GapParam_CursorPos], tgtPos)) {
      gap_pal_window_cursor_pos_set(pal, win->id, tgtPos);
      win->params[GapParam_CursorPos] = tgtPos;
    }
  }
  dynstring_clear(&win->inputText);
  dynstring_append(&win->inputText, gap_pal_window_input_text(pal, win->id));

  string_maybe_free(g_allocHeap, win->clipPaste);
  if (palFlags & GapPalWindowFlags_ClipPaste) {
    win->clipPaste = string_dup(g_allocHeap, gap_pal_window_clip_paste_result(pal, win->id));
    win->events |= GapWindowEvents_ClipPaste;
  } else {
    win->clipPaste = string_empty;
  }

  if (window_should_close(win)) {
    ecs_world_entity_destroy(world, winEntity);
  }

  // All requests have been handled.
  win->requests = 0;
}

ecs_view_define(GapPlatformView) { ecs_access_write(GapPlatformComp); }

ecs_view_define(GapWindowView) {
  ecs_access_write(GapWindowComp);
  ecs_access_write(GapWindowAspectComp);
}

ecs_system_define(GapWindowUpdateSys) {
  GapPlatformComp* platform = ecs_utils_write_first_t(world, GapPlatformView, GapPlatformComp);
  if (!platform) {
    return;
  }

  EcsView* windowView = ecs_world_view_t(world, GapWindowView);
  for (EcsIterator* itr = ecs_view_itr(windowView); ecs_view_walk(itr);) {
    const EcsEntityId    winEntity = ecs_view_entity(itr);
    GapWindowComp*       win       = ecs_view_write_t(itr, GapWindowComp);
    GapWindowAspectComp* winAspect = ecs_view_write_t(itr, GapWindowAspectComp);

    window_update(world, platform, win, winAspect, winEntity);
  }

  gap_pal_flush(platform->pal);
}

ecs_module_init(gap_window_module) {
  ecs_register_comp(GapWindowComp, .destructor = ecs_destruct_window_comp, .destructOrder = 20);
  ecs_register_comp(GapWindowAspectComp);

  ecs_register_view(GapPlatformView);
  ecs_register_view(GapWindowView);

  EcsSystemFlags sysFlags = 0;
  if (gap_pal_require_thread_affinity()) {
    sysFlags |= EcsSystemFlags_ThreadAffinity;
  }
  ecs_register_system_with_flags(
      GapWindowUpdateSys, sysFlags, ecs_view_id(GapPlatformView), ecs_view_id(GapWindowView));
  ecs_order(GapWindowUpdateSys, GapOrder_WindowUpdate);
}

EcsEntityId gap_window_create(
    EcsWorld*            world,
    const GapWindowMode  mode,
    const GapWindowFlags flags,
    const GapVector      size,
    const GapIcon        icon,
    const String         title) {
  const EcsEntityId windowEntity = ecs_world_entity_create(world);
  GapWindowComp*    comp         = ecs_world_add_t(
      world,
      windowEntity,
      GapWindowComp,
      .id                          = sentinel_u32,
      .events                      = GapWindowEvents_Initializing,
      .mode                        = mode,
      .requests                    = GapWindowRequests_Create,
      .params[GapParam_WindowSize] = size,
      .inputText                   = dynstring_create(g_allocHeap, 64));

  gap_window_flags_set(comp, flags);
  gap_window_icon_set(comp, icon);
  if (!string_is_empty(title)) {
    gap_window_title_set(comp, title);
  }

  ecs_world_add_t(world, windowEntity, GapWindowAspectComp, .ratio = window_aspect(size));

  return windowEntity;
}

void gap_window_close(GapWindowComp* window) { window->requests |= GapWindowRequests_Close; }

GapWindowFlags gap_window_flags(const GapWindowComp* window) { return window->flags; }

void gap_window_flags_set(GapWindowComp* comp, const GapWindowFlags flags) {
  if (flags & GapWindowFlags_CursorHide && !(comp->flags & GapWindowFlags_CursorHide)) {
    comp->requests |= GapWindowRequests_UpdateCursorHide;
  }
  if (flags & GapWindowFlags_CursorLock && !(comp->flags & GapWindowFlags_CursorLock)) {
    comp->requests |= GapWindowRequests_UpdateCursorLock;
  }
  if (flags & GapWindowFlags_CursorConfine && !(comp->flags & GapWindowFlags_CursorConfine)) {
    comp->requests |= GapWindowRequests_UpdateCursorConfine;
  }
  comp->flags |= flags;
}

void gap_window_flags_unset(GapWindowComp* comp, const GapWindowFlags flags) {
  if (flags & GapWindowFlags_CursorHide && comp->flags & GapWindowFlags_CursorHide) {
    comp->requests |= GapWindowRequests_UpdateCursorHide;
  }
  if (flags & GapWindowFlags_CursorLock && comp->flags & GapWindowFlags_CursorLock) {
    comp->requests |= GapWindowRequests_UpdateCursorLock;
  }
  if (flags & GapWindowFlags_CursorConfine && comp->flags & GapWindowFlags_CursorConfine) {
    comp->requests |= GapWindowRequests_UpdateCursorConfine;
  }
  comp->flags &= ~flags;
}

GapWindowEvents gap_window_events(const GapWindowComp* window) { return window->events; }

GapWindowMode gap_window_mode(const GapWindowComp* window) { return window->mode; }

void gap_window_resize(GapWindowComp* comp, const GapVector size, const GapWindowMode mode) {
  if (comp->mode != GapWindowMode_Fullscreen && mode == GapWindowMode_Fullscreen) {
    comp->params[GapParam_WindowSizePreFullscreen] = comp->params[GapParam_WindowSize];
  }
  comp->params[GapParam_WindowSize] = size;
  comp->mode                        = mode;
  comp->requests |= GapWindowRequests_Resize;
}

String gap_window_title_get(const GapWindowComp* window) { return window->title; }

void gap_window_title_set(GapWindowComp* window, const String newTitle) {
  string_maybe_free(g_allocHeap, window->title);
  window->title = string_maybe_dup(g_allocHeap, newTitle);
  window->requests |= GapWindowRequests_UpdateTitle;
}

GapVector gap_window_param(const GapWindowComp* comp, const GapParam param) {
  return comp->params[param];
}

bool gap_window_key_pressed(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysPressed, key);
}

bool gap_window_key_pressed_with_repeat(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysPressedWithRepeat, key);
}

bool gap_window_key_released(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysReleased, key);
}

bool gap_window_key_down(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysDown, key);
}

void gap_window_icon_set(GapWindowComp* comp, const GapIcon icon) {
  if (comp->icon != icon) {
    comp->icon = icon;
    comp->requests |= GapWindowRequests_UpdateIconType;
  }
}

void gap_window_cursor_set(GapWindowComp* comp, const GapCursor cursor) {
  if (comp->cursor != cursor) {
    comp->cursor = cursor;
    comp->requests |= GapWindowRequests_UpdateCursorType;
  }
}

String gap_window_input_text(const GapWindowComp* comp) { return dynstring_view(&comp->inputText); }

void gap_window_clip_copy(GapWindowComp* comp, const String value) {
  string_maybe_free(g_allocHeap, comp->clipCopy);
  comp->clipCopy = string_maybe_dup(g_allocHeap, value);
}

void gap_window_clip_paste(GapWindowComp* comp) { comp->requests |= GapWindowRequests_ClipPaste; }

String gap_window_clip_paste_result(const GapWindowComp* comp) { return comp->clipPaste; }

String gap_window_display_name(const GapWindowComp* comp) { return comp->displayName; }

f32 gap_window_refresh_rate(const GapWindowComp* comp) { return comp->refreshRate; }

u16 gap_window_dpi(const GapWindowComp* comp) { return comp->dpi; }

TimeDuration gap_window_doubleclick_interval(const GapWindowComp* comp) {
  (void)comp;
  return gap_pal_doubleclick_interval();
}

GapNativeWm gap_native_wm(void) { return gap_pal_native_wm(); }

uptr gap_native_window_handle(const GapWindowComp* comp) { return (uptr)comp->id; }

uptr gap_native_app_handle(const GapWindowComp* comp) { return comp->nativeAppHandle; }
