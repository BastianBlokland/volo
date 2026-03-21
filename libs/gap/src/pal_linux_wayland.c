#include "core/alloc.h"
#include "core/diag.h"
#include "core/dynarray.h"
#include "core/dynstring.h"
#include "log/logger.h"

#include "pal.h"

/**
 * Wayland client implementation.
 *
 * Standard: https://wayland.freedesktop.org/docs/html/
 * Protocol: https://wayland.app/protocols/wayland
 */

typedef struct {
  GapWindowId       id;
  GapVector         params[GapParam_Count];
  GapPalWindowFlags flags : 16;
  GapKeySet         keysPressed, keysPressedWithRepeat, keysReleased, keysDown;
  DynString         inputText;
  String            clipPaste;
  String            displayName;
  f32               refreshRate;
  u16               dpi;
} GapPalWindow;

struct sGapPal {
  Allocator* alloc;
  DynArray   windows; // GapPalWindow[]
};

static GapPalWindow* pal_maybe_window(GapPal* pal, const GapWindowId id) {
  dynarray_for_t(&pal->windows, GapPalWindow, window) {
    if (window->id == id) {
      return window;
    }
  }
  return null;
}

static GapPalWindow* pal_window(GapPal* pal, const GapWindowId id) {
  GapPalWindow* window = pal_maybe_window(pal, id);
  if (UNLIKELY(!window)) {
    diag_crash_msg("Unknown window: {}", fmt_int(id));
  }
  return window;
}

static void pal_clear_volatile(GapPal* pal) {
  dynarray_for_t(&pal->windows, GapPalWindow, window) {
    gap_keyset_clear(&window->keysPressed);
    gap_keyset_clear(&window->keysPressedWithRepeat);
    gap_keyset_clear(&window->keysReleased);

    window->params[GapParam_ScrollDelta] = gap_vector(0, 0);

    window->flags &= ~GapPalWindowFlags_Volatile;

    dynstring_clear(&window->inputText);

    string_maybe_free(pal->alloc, window->clipPaste);
    window->clipPaste = string_empty;
  }
}

GapPal* gap_pal_create(Allocator* alloc) {
  GapPal* pal = alloc_alloc_t(alloc, GapPal);

  log_i("Wayland initialized");

  *pal = (GapPal){
      .alloc   = alloc,
      .windows = dynarray_create_t(alloc, GapPalWindow, 1),
  };
  return pal;
}

void gap_pal_destroy(GapPal* pal) {
  while (pal->windows.size) {
    gap_pal_window_destroy(pal, dynarray_at_t(&pal->windows, 0, GapPalWindow)->id);
  }
  dynarray_destroy(&pal->windows);
  alloc_free_t(pal->alloc, pal);
}

void gap_pal_update(GapPal* pal) {
  // Clear volatile state, like the key-presses from the previous update.
  pal_clear_volatile(pal);
}

void gap_pal_flush(GapPal* pal) {
  (void)pal;
  diag_crash_msg("Not implemented");
}

void gap_pal_icon_load(GapPal* pal, const GapIcon icon, const AssetIconComp* asset) {
  (void)pal;
  (void)icon;
  (void)asset;
  diag_crash_msg("Not implemented");
}

void gap_pal_cursor_load(GapPal* pal, const GapCursor cursor, const AssetIconComp* asset) {
  (void)pal;
  (void)cursor;
  (void)asset;
  diag_crash_msg("Not implemented");
}

bool gap_pal_key_label(const GapPal* pal, const GapKey key, DynString* out) {
  (void)pal;
  (void)key;
  (void)out;
  diag_crash_msg("Not implemented");
}

GapWindowId gap_pal_window_create(GapPal* pal, const GapVector size) {
  (void)size;

  const GapWindowId id = 42; // TODO: Allocate ids.

  *dynarray_push_t(&pal->windows, GapPalWindow) = (GapPalWindow){
      .id        = id,
      .inputText = dynstring_create(pal->alloc, 64),
  };

  diag_crash_msg("Not implemented");
}

void gap_pal_window_destroy(GapPal* pal, const GapWindowId windowId) {
  for (usize i = 0; i != pal->windows.size; ++i) {
    GapPalWindow* window = dynarray_at_t(&pal->windows, i, GapPalWindow);
    if (window->id == windowId) {
      dynstring_destroy(&window->inputText);
      string_maybe_free(pal->alloc, window->clipPaste);
      string_maybe_free(pal->alloc, window->displayName);
      dynarray_remove_unordered(&pal->windows, i, 1);
      break;
    }
  }

  log_i("Window destroyed", log_param("id", fmt_int(windowId)));
}

GapPalWindowFlags gap_pal_window_flags(const GapPal* pal, const GapWindowId windowId) {
  return pal_window((GapPal*)pal, windowId)->flags;
}

GapVector
gap_pal_window_param(const GapPal* pal, const GapWindowId windowId, const GapParam param) {
  return pal_window((GapPal*)pal, windowId)->params[param];
}

const GapKeySet* gap_pal_window_keys_pressed(const GapPal* pal, const GapWindowId windowId) {
  return &pal_window((GapPal*)pal, windowId)->keysPressed;
}

const GapKeySet*
gap_pal_window_keys_pressed_with_repeat(const GapPal* pal, const GapWindowId windowId) {
  return &pal_window((GapPal*)pal, windowId)->keysPressedWithRepeat;
}

const GapKeySet* gap_pal_window_keys_released(const GapPal* pal, const GapWindowId windowId) {
  return &pal_window((GapPal*)pal, windowId)->keysReleased;
}

const GapKeySet* gap_pal_window_keys_down(const GapPal* pal, const GapWindowId windowId) {
  return &pal_window((GapPal*)pal, windowId)->keysDown;
}

String gap_pal_window_input_text(const GapPal* pal, const GapWindowId windowId) {
  return dynstring_view(&pal_window((GapPal*)pal, windowId)->inputText);
}

void gap_pal_window_title_set(GapPal* pal, const GapWindowId windowId, const String title) {
  (void)pal;
  (void)windowId;
  (void)title;
  diag_crash_msg("Not implemented");
}

void gap_pal_window_resize(
    GapPal* pal, const GapWindowId windowId, GapVector size, const bool fullscreen) {
  (void)pal;
  (void)windowId;
  (void)size;
  (void)fullscreen;
  diag_crash_msg("Not implemented");
}

void gap_pal_window_cursor_hide(GapPal* pal, const GapWindowId windowId, const bool hidden) {
  (void)pal;
  (void)windowId;
  (void)hidden;
  diag_crash_msg("Not implemented");
}

void gap_pal_window_cursor_capture(GapPal* pal, const GapWindowId windowId, const bool captured) {
  (void)pal;
  (void)windowId;
  (void)captured;
  diag_crash_msg("Not implemented");
}

void gap_pal_window_cursor_confine(GapPal* pal, const GapWindowId windowId, const bool confined) {
  (void)pal;
  (void)windowId;
  (void)confined;
  diag_crash_msg("Not implemented");
}

void gap_pal_window_icon_set(GapPal* pal, const GapWindowId windowId, const GapIcon icon) {
  (void)pal;
  (void)windowId;
  (void)icon;
  diag_crash_msg("Not implemented");
}

void gap_pal_window_cursor_set(GapPal* pal, const GapWindowId windowId, const GapCursor cursor) {
  (void)pal;
  (void)windowId;
  (void)cursor;
  diag_crash_msg("Not implemented");
}

void gap_pal_window_cursor_pos_set(
    GapPal* pal, const GapWindowId windowId, const GapVector position) {
  (void)pal;
  (void)windowId;
  (void)position;
  diag_crash_msg("Not implemented");
}

void gap_pal_window_clip_copy(GapPal* pal, const GapWindowId windowId, const String value) {
  (void)pal;
  (void)windowId;
  (void)value;
  diag_crash_msg("Not implemented");
}

void gap_pal_window_clip_paste(GapPal* pal, const GapWindowId windowId) {
  (void)pal;
  (void)windowId;
  diag_crash_msg("Not implemented");
}

String gap_pal_window_clip_paste_result(GapPal* pal, const GapWindowId windowId) {
  return pal_maybe_window(pal, windowId)->clipPaste;
}

String gap_pal_window_display_name(GapPal* pal, const GapWindowId windowId) {
  return pal_maybe_window(pal, windowId)->displayName;
}

f32 gap_pal_window_refresh_rate(GapPal* pal, const GapWindowId windowId) {
  return pal_maybe_window(pal, windowId)->refreshRate;
}

u16 gap_pal_window_dpi(GapPal* pal, const GapWindowId windowId) {
  return pal_maybe_window(pal, windowId)->dpi;
}

TimeDuration gap_pal_doubleclick_interval(void) {
  /**
   * Unfortunately Wayland does not expose the concept of the system's 'double click time'.
   */
  return time_milliseconds(500);
}

bool gap_pal_require_thread_affinity(void) {
  /**
   * No thread-affinity required for Wayland, meaning we can call it from different threads.
   */
  return false;
}

GapNativeWm gap_pal_native_wm(void) { return GapNativeWm_Wayland; }

uptr gap_pal_native_app_handle(const GapPal* pal) {
  (void)pal;
  diag_crash_msg("Not implemented");
}

void gap_pal_modal_error(String message) { (void)message; }
