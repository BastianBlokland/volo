#include "core/alloc.h"
#include "core/diag.h"
#include "core/dynarray.h"
#include "core/dynlib.h"
#include "core/dynstring.h"
#include "core/math.h"
#include "log/logger.h"
#include "wayland/wayland.h"

#include "pal.h"

static const char* to_null_term_scratch(const String str) {
  const Mem mem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(mem, str);
  *mem_at_u8(mem, str.size) = '\0';
  return mem.ptr;
}

/**
 * Wayland client implementation.
 *
 * Standard: https://wayland.freedesktop.org/docs/html/
 * Protocol: https://wayland.app/protocols/wayland
 */

static const i32 pal_window_min_width  = 128;
static const i32 pal_window_min_height = 128;

#define pal_window_default_refresh_rate 60.0f
#define pal_window_default_dpi 96

// Linux input event button codes (from linux/input-event-codes.h).
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114

typedef struct {
  struct wl_output* wlOutput;
  String            name; // heap-allocated via g_allocHeap
  i32               physWidthMm, physHeightMm;
  i32               modeWidthPx, modeHeightPx;
  i32               refreshMHz;
  i32               scale; // integer buffer scale factor (1 if not set)
} WlOutput;

typedef struct {
  DynLib*             lib;
  WlFuncs             api;
  struct wl_display*  display;
  struct wl_registry* registry;

  struct wl_compositor* compositor;
  u32                   compositorVersion;
  struct xdg_wm_base*   xdgWmBase;
  u32                   xdgWmBaseVersion;

  struct wl_seat*    seat;
  struct wl_pointer* pointer;

  struct wp_fractional_scale_manager_v1* fracScaleManager;
  struct wp_viewporter*                  viewporter;

  DynArray outputs; // WlOutput[]

  struct wl_registry_listener              registryListener;
  struct xdg_wm_base_listener              xdgWmBaseListener;
  struct wl_seat_listener                  seatListener;
  struct wl_pointer_listener               pointerListener;
  struct wl_output_listener                outputListener;
  struct wl_surface_listener               surfaceListener;
  struct wp_fractional_scale_v1_listener   fracScaleListener;
} Wayland;

typedef struct {
  GapVector         params[GapParam_Count];
  GapPalWindowFlags flags : 16;
  GapKeySet         keysPressed, keysPressedWithRepeat, keysReleased, keysDown;
  DynString         inputText;
  String            clipPaste;
  String            displayName;
  f32               refreshRate;
  u16               dpi;

  u32                            surfaceScale120; // fractional scale: physical = ceil(logical * scale120 / 120)
  i32                            logicalWidth, logicalHeight; // last configure logical size
  Wayland*                       wl;
  struct wl_surface*             wlSurface;
  struct xdg_surface*            xdgSurface;
  struct xdg_toplevel*           xdgToplevel;
  struct wp_fractional_scale_v1* fracScale; // null if protocol unavailable
  struct wp_viewport*            viewport;  // null if protocol unavailable

  struct xdg_surface_listener  xdgSurfaceListener;
  struct xdg_toplevel_listener xdgToplevelListener;
} GapPalWindow;

struct sGapPal {
  Allocator* alloc;
  DynArray   windows; // GapPalWindow[]

  Wayland       wl;
  GapPalWindow* pointerFocus; // Window currently under the pointer (null if none).
};

static GapPalWindow* pal_maybe_window(GapPal* pal, const GapWindowId id) {
  dynarray_for_t(&pal->windows, GapPalWindow, window) {
    if ((GapWindowId)window->wlSurface == id) {
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

// -- Event helpers --

static void pal_event_cursor(GapPalWindow* window, const GapVector newPos) {
  if (!gap_vector_equal(window->params[GapParam_CursorPos], newPos)) {
    window->params[GapParam_CursorPos] = newPos;
    window->flags |= GapPalWindowFlags_CursorMoved;
  }
}

static void pal_event_press(GapPalWindow* window, const GapKey key) {
  gap_keyset_set(&window->keysPressedWithRepeat, key);
  if (!gap_keyset_test(&window->keysDown, key)) {
    gap_keyset_set(&window->keysPressed, key);
    gap_keyset_set(&window->keysDown, key);
  }
  window->flags |= GapPalWindowFlags_KeyPressed;
}

static void pal_event_release(GapPalWindow* window, const GapKey key) {
  if (gap_keyset_test(&window->keysDown, key)) {
    gap_keyset_set(&window->keysReleased, key);
    gap_keyset_unset(&window->keysDown, key);
    window->flags |= GapPalWindowFlags_KeyReleased;
  }
}

static void pal_event_scroll(GapPalWindow* window, const GapVector delta) {
  window->params[GapParam_ScrollDelta].x += delta.x;
  window->params[GapParam_ScrollDelta].y += delta.y;
  window->flags |= GapPalWindowFlags_Scrolled;
}

static WlOutput* wl_find_output(Wayland* wl, struct wl_output* wlOutput) {
  dynarray_for_t(&wl->outputs, WlOutput, o) {
    if (o->wlOutput == wlOutput) {
      return o;
    }
  }
  return null;
}

static void
pal_event_display_name_changed(GapPal* pal, GapPalWindow* window, const String newName) {
  if (string_eq(window->displayName, newName)) {
    return;
  }
  string_maybe_free(pal->alloc, window->displayName);
  window->displayName = string_maybe_dup(pal->alloc, newName);
  window->flags |= GapPalWindowFlags_DisplayNameChanged;
  log_d(
      "Window display-name changed",
      log_param("id", fmt_int((uptr)window->wlSurface)),
      log_param("display-name", fmt_text(newName)));
}

static void pal_event_refresh_rate_changed(GapPalWindow* window, const f32 newRefreshRate) {
  if (window->refreshRate == newRefreshRate) {
    return;
  }
  window->refreshRate = newRefreshRate;
  window->flags |= GapPalWindowFlags_RefreshRateChanged;
  log_d(
      "Window refresh-rate changed",
      log_param("id", fmt_int((uptr)window->wlSurface)),
      log_param("refresh-rate", fmt_float(newRefreshRate)));
}

static void pal_event_dpi_changed(GapPalWindow* window, const u16 newDpi) {
  if (window->dpi == newDpi) {
    return;
  }
  window->dpi = newDpi;
  window->flags |= GapPalWindowFlags_DpiChanged;
  log_d(
      "Window dpi changed",
      log_param("id", fmt_int((uptr)window->wlSurface)),
      log_param("dpi", fmt_int(newDpi)));
}

static void pal_event_focus_gained(GapPalWindow* window) {
  if (window->flags & GapPalWindowFlags_Focused) {
    return;
  }
  window->flags |= GapPalWindowFlags_Focused | GapPalWindowFlags_FocusGained;
  log_d("Window focus gained", log_param("id", fmt_int((uptr)window->wlSurface)));
}

static void pal_event_focus_lost(GapPalWindow* window) {
  if (!(window->flags & GapPalWindowFlags_Focused)) {
    return;
  }
  window->flags &= ~GapPalWindowFlags_Focused;
  window->flags |= GapPalWindowFlags_FocusLost;
  gap_keyset_clear(&window->keysDown);
  log_d("Window focus lost", log_param("id", fmt_int((uptr)window->wlSurface)));
}

static GapKey pal_map_pointer_button(const u32 linuxButton) {
  switch (linuxButton) {
  case BTN_LEFT:
    return GapKey_MouseLeft;
  case BTN_RIGHT:
    return GapKey_MouseRight;
  case BTN_MIDDLE:
    return GapKey_MouseMiddle;
  case BTN_SIDE:
    return GapKey_MouseExtra1;
  case BTN_EXTRA:
    return GapKey_MouseExtra2;
  default:
    return GapKey_MouseLeft; // Fallback; should not be reached.
  }
}

// -- Wayland listener callbacks --

// -- wl_output callbacks --

static void wl_output_geometry(
    void*             data,
    struct wl_output* obj,
    i32               x,
    i32               y,
    i32               physWidth,
    i32               physHeight,
    i32               subpixel,
    const char*       make,
    const char*       model,
    i32               transform) {
  Wayland* wl = data;
  (void)x;
  (void)y;
  (void)subpixel;
  (void)make;
  (void)model;
  (void)transform;
  WlOutput* out = wl_find_output(wl, obj);
  if (out) {
    out->physWidthMm  = physWidth;
    out->physHeightMm = physHeight;
  }
}

static void
wl_output_mode(void* data, struct wl_output* obj, u32 flags, i32 width, i32 height, i32 refresh) {
  if (!(flags & WL_OUTPUT_MODE_CURRENT)) {
    return;
  }
  Wayland*  wl  = data;
  WlOutput* out = wl_find_output(wl, obj);
  if (out) {
    out->modeWidthPx  = width;
    out->modeHeightPx = height;
    out->refreshMHz   = refresh;
  }
}

static void wl_output_done(void* data, struct wl_output* obj) {
  Wayland*  wl  = data;
  WlOutput* out = wl_find_output(wl, obj);
  if (!out) {
    return;
  }
  const f32 refreshRate = out->refreshMHz / 1000.0f;
  u16       dpi         = pal_window_default_dpi;
  if (out->physWidthMm > 0) {
    dpi = (u16)math_round_nearest_f32(out->modeWidthPx * 25.4f / out->physWidthMm);
  }
  log_i(
      "Wayland output",
      log_param("name", fmt_text(out->name)),
      log_param("scale", fmt_int(out->scale)),
      log_param("refresh-rate", fmt_float(refreshRate)),
      log_param("dpi", fmt_int(dpi)));
}

static void wl_output_scale(void* data, struct wl_output* obj, i32 factor) {
  Wayland*  wl  = data;
  WlOutput* out = wl_find_output(wl, obj);
  if (out) {
    out->scale = factor;
  }
}

static void wl_output_name(void* data, struct wl_output* obj, const char* name) {
  Wayland*  wl  = data;
  WlOutput* out = wl_find_output(wl, obj);
  if (out) {
    string_maybe_free(g_allocHeap, out->name);
    out->name = string_maybe_dup(g_allocHeap, string_from_null_term(name));
  }
}

static void wl_output_description(void* data, struct wl_output* obj, const char* desc) {
  (void)data;
  (void)obj;
  (void)desc;
}

// -- Fractional scale helpers --

/**
 * Convert a logical size to physical pixels using fractional scale (in 1/120ths).
 * physical = ceil(logical * scale120 / 120)
 */
static i32 pal_logical_to_physical(const i32 logical, const u32 scale120) {
  return (i32)(((i64)logical * scale120 + 119) / 120);
}

static void wp_fractional_scale_preferred_scale(
    void* data, struct wp_fractional_scale_v1* obj, u32 scale) {
  GapPalWindow* window = data;
  (void)obj;
  if (window->surfaceScale120 == scale) {
    return;
  }
  window->surfaceScale120 = scale;
  // Recompute physical size from stored logical size (if a configure has been received).
  if (window->logicalWidth > 0 && window->logicalHeight > 0) {
    if (window->viewport) {
      wp_viewport_set_destination(
          &window->wl->api, window->viewport, window->logicalWidth, window->logicalHeight);
    }
    const GapVector newSize = gap_vector(
        pal_logical_to_physical(window->logicalWidth, scale),
        pal_logical_to_physical(window->logicalHeight, scale));
    if (!gap_vector_equal(window->params[GapParam_WindowSize], newSize)) {
      window->params[GapParam_WindowSize] = newSize;
      window->flags |= GapPalWindowFlags_Resized;
    }
  }
  log_d(
      "Surface fractional scale changed",
      log_param("id", fmt_int((uptr)window->wlSurface)),
      log_param("scale120", fmt_int(scale)));
}

// -- wl_surface callbacks --

static void wl_surface_enter(void* data, struct wl_surface* surface, struct wl_output* output) {
  GapPal*       pal    = data;
  GapPalWindow* window = pal_maybe_window(pal, (GapWindowId)surface);
  WlOutput*     out    = wl_find_output(&pal->wl, output);
  if (!window || !out) {
    return;
  }
  pal_event_display_name_changed(pal, window, out->name);
  pal_event_refresh_rate_changed(window, out->refreshMHz / 1000.0f);
  u16 dpi = pal_window_default_dpi;
  if (out->physWidthMm > 0) {
    dpi = (u16)math_round_nearest_f32(out->modeWidthPx * 25.4f / out->physWidthMm);
  }
  pal_event_dpi_changed(window, dpi);
}

static void wl_surface_leave(void* data, struct wl_surface* surface, struct wl_output* output) {
  (void)data;
  (void)surface;
  (void)output;
}

static void
wl_surface_preferred_buffer_scale(void* data, struct wl_surface* surface, i32 factor) {
  GapPal*       pal    = data;
  GapPalWindow* window = pal_maybe_window(pal, (GapWindowId)surface);
  if (!window || window->fracScale) {
    return; // fractional scale protocol provides a more precise value
  }
  const u32 scale120 = (u32)factor * 120;
  if (window->surfaceScale120 == scale120) {
    return;
  }
  window->surfaceScale120 = scale120;
  wl_surface_set_buffer_scale(&window->wl->api, surface, factor);
}

static void
wl_surface_noop_preferred_buffer_transform(void* data, struct wl_surface* surface, u32 transform) {
  (void)data;
  (void)surface;
  (void)transform;
}

// -- Registry --

static void wl_registry_global(
    void* data, struct wl_registry* registry, u32 name, const char* interface, u32 version) {
  Wayland* wl = data;
  if (string_eq(string_from_null_term(interface), string_lit("wl_compositor"))) {
    wl->compositor = wlRegistryBind(&wl->api, registry, name, version, &wl_compositor_interface, 6);
    wl->compositorVersion = wl_compositor_get_version(&wl->api, wl->compositor);
  } else if (string_eq(string_from_null_term(interface), string_lit("xdg_wm_base"))) {
    wl->xdgWmBase = wlRegistryBind(&wl->api, registry, name, version, &xdg_wm_base_interface, 1);
    wl->xdgWmBaseVersion = xdg_wm_base_get_version(&wl->api, wl->xdgWmBase);
  } else if (string_eq(string_from_null_term(interface), string_lit("wl_seat"))) {
    wl->seat = wlRegistryBind(&wl->api, registry, name, version, &wl_seat_interface, 9);
  } else if (string_eq(string_from_null_term(interface), string_lit("wl_output"))) {
    WlOutput* out = dynarray_push_t(&wl->outputs, WlOutput);
    *out          = (WlOutput){
                 .wlOutput = wlRegistryBind(&wl->api, registry, name, version, &wl_output_interface, 4),
    };
    wl_output_add_listener(&wl->api, out->wlOutput, &wl->outputListener, wl);
  } else if (string_eq(
                 string_from_null_term(interface),
                 string_lit("wp_fractional_scale_manager_v1"))) {
    wl->fracScaleManager = wlRegistryBind(
        &wl->api, registry, name, version, &wp_fractional_scale_manager_v1_interface, 1);
  } else if (string_eq(string_from_null_term(interface), string_lit("wp_viewporter"))) {
    wl->viewporter =
        wlRegistryBind(&wl->api, registry, name, version, &wp_viewporter_interface, 1);
  }
}

static void wl_registry_global_remove(void* data, struct wl_registry* reg, u32 name) {
  (void)data;
  (void)reg;
  (void)name;
}

static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdgWmBase, u32 serial) {
  Wayland* wl = data;
  xdg_wm_base_pong(&wl->api, xdgWmBase, serial);
}

static void xdg_surface_configure(void* data, struct xdg_surface* xdgSurface, u32 serial) {
  GapPalWindow* window = data;
  xdg_surface_ack_configure(&window->wl->api, xdgSurface, serial);
  wl_surface_commit(&window->wl->api, window->wlSurface);
}

static void xdg_toplevel_configure(
    void* data, struct xdg_toplevel* top, i32 width, i32 height, struct wl_array* states) {
  GapPalWindow* window = data;
  (void)top;

  // Parse the states array to detect fullscreen and activation.
  bool             fullscreen = false, activated = false;
  const u32* const stateEnd = (const u32*)((u8*)states->data + states->size);
  for (const u32* s = states->data; s != stateEnd; ++s) {
    if (*s == XDG_TOPLEVEL_STATE_FULLSCREEN) {
      fullscreen = true;
    }
    if (*s == XDG_TOPLEVEL_STATE_ACTIVATED) {
      activated = true;
    }
  }
  if (fullscreen) {
    window->flags |= GapPalWindowFlags_Fullscreen;
  } else {
    window->flags &= ~GapPalWindowFlags_Fullscreen;
  }
  if (activated) {
    pal_event_focus_gained(window);
  } else {
    pal_event_focus_lost(window);
  }

  if (width > 0 && height > 0) {
    window->logicalWidth  = width;
    window->logicalHeight = height;
    if (window->viewport) {
      // Tell the compositor how the physical buffer maps to the logical window area.
      wp_viewport_set_destination(&window->wl->api, window->viewport, width, height);
    }
    // xdg_toplevel configure gives logical (surface) coords; convert to physical pixels.
    const GapVector newSize = gap_vector(
        pal_logical_to_physical(width, window->surfaceScale120),
        pal_logical_to_physical(height, window->surfaceScale120));
    if (!gap_vector_equal(window->params[GapParam_WindowSize], newSize)) {
      window->params[GapParam_WindowSize] = newSize;
      window->flags |= GapPalWindowFlags_Resized;

      log_d(
          "Window resized",
          log_param("id", fmt_int((uptr)window->wlSurface)),
          log_param("size", gap_vector_fmt(newSize)));
    }
  }
}

static void xdg_toplevel_close(void* data, struct xdg_toplevel* top) {
  GapPalWindow* window = data;
  (void)top;
  window->flags |= GapPalWindowFlags_CloseRequested;
}

static void xdg_toplevel_noop_configure_bounds(void* data, struct xdg_toplevel* top, i32 w, i32 h) {
  (void)data;
  (void)top;
  (void)w;
  (void)h;
}

static void
xdg_toplevel_noop_wm_capabilities(void* data, struct xdg_toplevel* top, struct wl_array* caps) {
  (void)data;
  (void)top;
  (void)caps;
}

static void wl_pointer_enter(
    void*              data,
    struct wl_pointer* ptr,
    u32                serial,
    struct wl_surface* surface,
    i32                surfaceX,
    i32                surfaceY) {
  GapPal* pal = data;
  (void)ptr;
  (void)serial;
  (void)surfaceX;
  (void)surfaceY;
  pal->pointerFocus = pal_maybe_window(pal, (GapWindowId)surface);
}

static void
wl_pointer_leave(void* data, struct wl_pointer* ptr, u32 serial, struct wl_surface* surface) {
  GapPal* pal = data;
  (void)ptr;
  (void)serial;
  (void)surface;
  pal->pointerFocus = null;
}

static void
wl_pointer_motion(void* data, struct wl_pointer* ptr, u32 time, i32 surfaceX, i32 surfaceY) {
  GapPal* pal = data;
  (void)ptr;
  (void)time;
  if (!pal->pointerFocus) {
    return;
  }
  GapPalWindow* window       = pal->pointerFocus;
  const i32     windowHeight = (i32)window->params[GapParam_WindowSize].height;
  // wl_fixed_t is 24.8 fixed-point; shift right 8 bits to get logical pixels.
  // Convert logical to physical and flip y (Wayland top-left, GapVector bottom-left).
  const i32       physX = pal_logical_to_physical(surfaceX >> 8, window->surfaceScale120);
  const i32       physY = pal_logical_to_physical(surfaceY >> 8, window->surfaceScale120);
  const GapVector pos   = gap_vector(physX, windowHeight - physY);
  pal_event_cursor(window, pos);
}

static void
wl_pointer_button(void* data, struct wl_pointer* ptr, u32 serial, u32 time, u32 button, u32 state) {
  GapPal* pal = data;
  (void)ptr;
  (void)serial;
  (void)time;
  if (!pal->pointerFocus) {
    return;
  }
  const GapKey key = pal_map_pointer_button(button);
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    pal_event_press(pal->pointerFocus, key);
  } else {
    pal_event_release(pal->pointerFocus, key);
  }
}

static void wl_pointer_axis(void* data, struct wl_pointer* ptr, u32 time, u32 axis, i32 value) {
  GapPal* pal = data;
  (void)ptr;
  (void)time;
  if (!pal->pointerFocus) {
    return;
  }
  // wl_fixed_t is 24.8 fixed-point; shift right 8 to get integer pixels.
  // Positive axis = scroll down/right; negate y to match our bottom-left convention.
  const i32       pixels = value >> 8;
  const GapVector delta =
      axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? gap_vector(0, -pixels) : gap_vector(pixels, 0);
  pal_event_scroll(pal->pointerFocus, delta);
}

static void wl_pointer_frame(void* data, struct wl_pointer* ptr) {
  (void)data;
  (void)ptr;
}

static void wl_pointer_axis_source(void* data, struct wl_pointer* ptr, u32 axisSource) {
  (void)data;
  (void)ptr;
  (void)axisSource;
}

static void wl_pointer_axis_stop(void* data, struct wl_pointer* ptr, u32 time, u32 axis) {
  (void)data;
  (void)ptr;
  (void)time;
  (void)axis;
}

static void wl_pointer_axis_discrete(void* data, struct wl_pointer* ptr, u32 axis, i32 discrete) {
  (void)data;
  (void)ptr;
  (void)axis;
  (void)discrete;
  // Scroll is handled via the axis event (always sent alongside axis_discrete).
}

static void wl_pointer_axis_value120(void* data, struct wl_pointer* ptr, u32 axis, i32 value120) {
  (void)data;
  (void)ptr;
  (void)axis;
  (void)value120;
  // Scroll is handled via the axis event (sent for all sources) in wl_pointer_frame.
}

static void
wl_pointer_axis_relative_direction(void* data, struct wl_pointer* ptr, u32 axis, u32 direction) {
  (void)data;
  (void)ptr;
  (void)axis;
  (void)direction;
}

static void wl_seat_capabilities(void* data, struct wl_seat* seat, u32 caps) {
  GapPal* pal = data;
  (void)seat;
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pal->wl.pointer) {
    pal->wl.pointer         = wl_seat_get_pointer(&pal->wl.api, pal->wl.seat);
    pal->wl.pointerListener = (struct wl_pointer_listener){
        .enter                   = wl_pointer_enter,
        .leave                   = wl_pointer_leave,
        .motion                  = wl_pointer_motion,
        .button                  = wl_pointer_button,
        .axis                    = wl_pointer_axis,
        .frame                   = wl_pointer_frame,
        .axis_source             = wl_pointer_axis_source,
        .axis_stop               = wl_pointer_axis_stop,
        .axis_discrete           = wl_pointer_axis_discrete,
        .axis_value120           = wl_pointer_axis_value120,
        .axis_relative_direction = wl_pointer_axis_relative_direction,
    };
    wl_pointer_add_listener(&pal->wl.api, pal->wl.pointer, &pal->wl.pointerListener, pal);
  }
}

static void wl_seat_name(void* data, struct wl_seat* seat, const char* name) {
  (void)data;
  (void)seat;
  (void)name;
}

// -- Initialization --

static void pal_init_wl_outputs(GapPal* pal) {
  pal->wl.surfaceListener = (struct wl_surface_listener){
      .enter                      = wl_surface_enter,
      .leave                      = wl_surface_leave,
      .preferred_buffer_scale     = wl_surface_preferred_buffer_scale,
      .preferred_buffer_transform = wl_surface_noop_preferred_buffer_transform,
  };
}

static bool pal_init_wl(Allocator* alloc, Wayland* out) {
  out->outputs        = dynarray_create_t(alloc, WlOutput, 4);
  out->fracScaleListener = (struct wp_fractional_scale_v1_listener){
      .preferred_scale = wp_fractional_scale_preferred_scale,
  };
  out->outputListener = (struct wl_output_listener){
      .geometry    = wl_output_geometry,
      .mode        = wl_output_mode,
      .done        = wl_output_done,
      .scale       = wl_output_scale,
      .name        = wl_output_name,
      .description = wl_output_description,
  };

  DynLibResult res = dynlib_load(alloc, string_lit("libwayland-client.so"), &out->lib);
  if (res != DynLibResult_Success) {
    log_e(
        "Failed to load Wayland ('libwayland-client.so')",
        log_param("error", fmt_text(dynlib_result_str(res))));
    return false;
  }

  if (!wlLoad(out->lib, &out->api)) {
    log_e("Wayland: missing symbols in 'libwayland-client.so'");
    dynlib_destroy(out->lib);
    return false;
  }

  out->display = out->api.display_connect(null);
  if (!out->display) {
    log_e("Wayland: failed to connect to display");
    dynlib_destroy(out->lib);
    return false;
  }

  out->registry = wl_display_get_registry(&out->api, out->display);

  out->registryListener = (struct wl_registry_listener){
      .global        = wl_registry_global,
      .global_remove = wl_registry_global_remove,
  };
  wl_registry_add_listener(&out->api, out->registry, &out->registryListener, out);

  out->api.display_roundtrip(out->display);

  if (!out->compositor) {
    log_e("Wayland: wl_compositor global not found");
    wl_registry_destroy(&out->api, out->registry);
    out->api.display_disconnect(out->display);
    dynlib_destroy(out->lib);
    return false;
  }
  if (!out->xdgWmBase) {
    log_e("Wayland: xdg_wm_base global not found");
    wl_compositor_destroy(&out->api, out->compositor);
    wl_registry_destroy(&out->api, out->registry);
    out->api.display_disconnect(out->display);
    dynlib_destroy(out->lib);
    return false;
  }

  out->xdgWmBaseListener = (struct xdg_wm_base_listener){
      .ping = xdg_wm_base_ping,
  };
  xdg_wm_base_add_listener(&out->api, out->xdgWmBase, &out->xdgWmBaseListener, out);

  return true;
}

static void pal_init_wl_seat(GapPal* pal) {
  if (!pal->wl.seat) {
    log_w("Wayland: wl_seat global not found; mouse and keyboard input unavailable");
    return;
  }
  pal->wl.seatListener = (struct wl_seat_listener){
      .capabilities = wl_seat_capabilities,
      .name         = wl_seat_name,
  };
  wl_seat_add_listener(&pal->wl.api, pal->wl.seat, &pal->wl.seatListener, pal);
  pal->wl.api.display_roundtrip(pal->wl.display);
}

static void pal_destroy_wl(Wayland* wl) {
  if (wl->pointer) {
    wl_pointer_release(&wl->api, wl->pointer);
  }
  if (wl->seat) {
    wl_seat_release(&wl->api, wl->seat);
  }
  dynarray_for_t(&wl->outputs, WlOutput, out) {
    string_maybe_free(g_allocHeap, out->name);
    wl_output_release(&wl->api, out->wlOutput);
  }
  dynarray_destroy(&wl->outputs);
  if (wl->viewporter) {
    wp_viewporter_destroy(&wl->api, wl->viewporter);
  }
  if (wl->fracScaleManager) {
    wp_fractional_scale_manager_v1_destroy(&wl->api, wl->fracScaleManager);
  }
  xdg_wm_base_destroy(&wl->api, wl->xdgWmBase);
  wl_compositor_destroy(&wl->api, wl->compositor);
  wl_registry_destroy(&wl->api, wl->registry);
  wl->api.display_disconnect(wl->display);
  dynlib_destroy(wl->lib);
}

// -- GapPal API --

GapPal* gap_pal_create(Allocator* alloc) {
  Wayland wl = {0};
  if (!pal_init_wl(alloc, &wl)) {
    return null;
  }

  log_i(
      "Wayland initialized",
      log_param("compositor-version", fmt_int(wl.compositorVersion)),
      log_param("xdg-wm-base-version", fmt_int(wl.xdgWmBaseVersion)));

  GapPal* pal = alloc_alloc_t(alloc, GapPal);
  *pal        = (GapPal){
             .alloc   = alloc,
             .windows = dynarray_create_t(alloc, GapPalWindow, 1),
             .wl      = wl,
  };

  pal_init_wl_seat(pal);
  pal_init_wl_outputs(pal);

  return pal;
}

void gap_pal_destroy(GapPal* pal) {
  while (pal->windows.size) {
    gap_pal_window_destroy(
        pal, (GapWindowId)dynarray_at_t(&pal->windows, 0, GapPalWindow)->wlSurface);
  }
  dynarray_destroy(&pal->windows);
  pal_destroy_wl(&pal->wl);
  alloc_free_t(pal->alloc, pal);
}

void gap_pal_update(GapPal* pal) {
  pal_clear_volatile(pal);
  pal->wl.api.display_dispatch_pending(pal->wl.display);
}

void gap_pal_flush(GapPal* pal) { pal->wl.api.display_flush(pal->wl.display); }

void gap_pal_icon_load(GapPal* pal, const GapIcon icon, const AssetIconComp* asset) {
  (void)pal;
  (void)icon;
  (void)asset;
}

void gap_pal_cursor_load(GapPal* pal, const GapCursor cursor, const AssetIconComp* asset) {
  (void)pal;
  (void)cursor;
  (void)asset;
}

bool gap_pal_key_label(const GapPal* pal, const GapKey key, DynString* out) {
  (void)pal;
  (void)key;
  (void)out;
  return false;
}

GapWindowId gap_pal_window_create(GapPal* pal, const GapVector size) {
  Wayland* wl = &pal->wl;

  struct wl_surface*   wlSurface  = wl_compositor_create_surface(&wl->api, wl->compositor);
  struct xdg_surface*  xdgSurface = xdg_wm_base_get_xdg_surface(&wl->api, wl->xdgWmBase, wlSurface);
  struct xdg_toplevel* xdgToplevel = xdg_surface_get_toplevel(&wl->api, xdgSurface);
  xdg_toplevel_set_app_id(&wl->api, xdgToplevel, "volo");

  // Use the integer scale of the first known output as initial estimate.
  // Updated to the exact fractional scale via wp_fractional_scale_v1.preferred_scale.
  const i32 initIntScale =
      wl->outputs.size ? math_max(dynarray_at_t(&wl->outputs, 0, WlOutput)->scale, 1) : 1;

  GapPalWindow* window = dynarray_push_t(&pal->windows, GapPalWindow);
  *window              = (GapPalWindow){
                   .inputText      = dynstring_create(pal->alloc, 64),
                   .refreshRate    = pal_window_default_refresh_rate,
                   .dpi            = pal_window_default_dpi,
                   .surfaceScale120 = (u32)initIntScale * 120,
                   .wl             = wl,
                   .wlSurface      = wlSurface,
                   .xdgSurface     = xdgSurface,
                   .xdgToplevel    = xdgToplevel,
  };
  window->params[GapParam_WindowSize] = size;

  wl_surface_add_listener(&wl->api, wlSurface, &wl->surfaceListener, pal);

  if (wl->fracScaleManager && wl->viewporter) {
    // Fractional scaling: get exact scale and use viewport to map buffer to logical area.
    window->fracScale = wp_fractional_scale_manager_v1_get_fractional_scale(
        &wl->api, wl->fracScaleManager, wlSurface);
    wp_fractional_scale_v1_add_listener(
        &wl->api, window->fracScale, &wl->fracScaleListener, window);
    window->viewport = wp_viewporter_get_viewport(&wl->api, wl->viewporter, wlSurface);
  } else {
    // Fallback: tell compositor our buffer is in physical pixels.
    wl_surface_set_buffer_scale(&wl->api, wlSurface, initIntScale);
  }

  window->xdgSurfaceListener = (struct xdg_surface_listener){
      .configure = xdg_surface_configure,
  };
  xdg_surface_add_listener(&wl->api, xdgSurface, &window->xdgSurfaceListener, window);

  window->xdgToplevelListener = (struct xdg_toplevel_listener){
      .configure        = xdg_toplevel_configure,
      .close            = xdg_toplevel_close,
      .configure_bounds = xdg_toplevel_noop_configure_bounds,
      .wm_capabilities  = xdg_toplevel_noop_wm_capabilities,
  };
  xdg_toplevel_add_listener(&wl->api, xdgToplevel, &window->xdgToplevelListener, window);

  // Initial commit triggers the compositor to send a configure event.
  // Also fires preferred_scale, giving us the correct surfaceScale120.
  wl_surface_commit(&wl->api, wlSurface);
  wl->api.display_roundtrip(wl->display);

  // Now that surfaceScale120 is correct, compute the logical size and set the viewport
  // destination. Without this, the compositor has no viewport destination and assumes
  // buffer_scale=1, so a physical-pixel buffer appears at the wrong (too large) logical size.
  const i32 logW = (i32)((i64)size.width * 120 / window->surfaceScale120);
  const i32 logH = (i32)((i64)size.height * 120 / window->surfaceScale120);
  window->logicalWidth  = logW;
  window->logicalHeight = logH;
  if (window->viewport) {
    wp_viewport_set_destination(&wl->api, window->viewport, logW, logH);
  }
  wl_surface_commit(&wl->api, wlSurface);
  wl->api.display_roundtrip(wl->display);

  log_i("Wayland window created", log_param("surface", fmt_int((uptr)wlSurface)));
  return (GapWindowId)wlSurface;
}

void gap_pal_window_destroy(GapPal* pal, const GapWindowId windowId) {
  for (usize i = 0; i != pal->windows.size; ++i) {
    GapPalWindow* window = dynarray_at_t(&pal->windows, i, GapPalWindow);
    if ((GapWindowId)window->wlSurface != windowId) {
      continue;
    }
    if (pal->pointerFocus == window) {
      pal->pointerFocus = null;
    }
    Wayland* wl = &pal->wl;
    if (window->viewport) {
      wp_viewport_destroy(&wl->api, window->viewport);
    }
    if (window->fracScale) {
      wp_fractional_scale_v1_destroy(&wl->api, window->fracScale);
    }
    if (window->xdgToplevel) {
      xdg_toplevel_destroy(&wl->api, window->xdgToplevel);
    }
    if (window->xdgSurface) {
      xdg_surface_destroy(&wl->api, window->xdgSurface);
    }
    if (window->wlSurface) {
      wl_surface_destroy(&wl->api, window->wlSurface);
    }
    dynstring_destroy(&window->inputText);
    string_maybe_free(pal->alloc, window->clipPaste);
    string_maybe_free(pal->alloc, window->displayName);
    dynarray_remove_unordered(&pal->windows, i, 1);
    log_i("Window destroyed", log_param("id", fmt_int(windowId)));
    return;
  }
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
  GapPalWindow* window = pal_window(pal, windowId);
  xdg_toplevel_set_title(&window->wl->api, window->xdgToplevel, to_null_term_scratch(title));
}

void gap_pal_window_resize(
    GapPal* pal, const GapWindowId windowId, GapVector size, const bool fullscreen) {
  GapPalWindow* window = pal_window(pal, windowId);

  if (size.width < pal_window_min_width) {
    size.width = pal_window_min_width;
  }
  if (size.height < pal_window_min_height) {
    size.height = pal_window_min_height;
  }

  log_d(
      "Updating window size",
      log_param("id", fmt_int(windowId)),
      log_param("size", gap_vector_fmt(size)),
      log_param("fullscreen", fmt_bool(fullscreen)));

  if (fullscreen) {
    // Pass null for output to use whichever output the window is currently on.
    xdg_toplevel_set_fullscreen(&window->wl->api, window->xdgToplevel, null);
  } else {
    xdg_toplevel_unset_fullscreen(&window->wl->api, window->xdgToplevel);
    // Convert physical to logical and update viewport destination so the compositor knows
    // our logical size. Do not set max_size so the user can still resize the window.
    const u32 scale120  = window->surfaceScale120;
    const i32 logWidth  = (i32)((i64)size.width * 120 / scale120);
    const i32 logHeight = (i32)((i64)size.height * 120 / scale120);
    xdg_toplevel_set_min_size(&window->wl->api, window->xdgToplevel, pal_window_min_width, pal_window_min_height);
    xdg_toplevel_set_max_size(&window->wl->api, window->xdgToplevel, 0, 0);
    if (window->viewport) {
      wp_viewport_set_destination(&window->wl->api, window->viewport, logWidth, logHeight);
    }
    window->logicalWidth  = logWidth;
    window->logicalHeight = logHeight;
  }
  wl_surface_commit(&window->wl->api, window->wlSurface);
}

void gap_pal_window_cursor_hide(GapPal* pal, const GapWindowId windowId, const bool hidden) {
  (void)pal;
  (void)windowId;
  (void)hidden;
}

void gap_pal_window_cursor_capture(GapPal* pal, const GapWindowId windowId, const bool captured) {
  (void)pal;
  (void)windowId;
  (void)captured;
}

void gap_pal_window_cursor_confine(GapPal* pal, const GapWindowId windowId, const bool confined) {
  (void)pal;
  (void)windowId;
  (void)confined;
}

void gap_pal_window_icon_set(GapPal* pal, const GapWindowId windowId, const GapIcon icon) {
  (void)pal;
  (void)windowId;
  (void)icon;
}

void gap_pal_window_cursor_set(GapPal* pal, const GapWindowId windowId, const GapCursor cursor) {
  (void)pal;
  (void)windowId;
  (void)cursor;
}

void gap_pal_window_cursor_pos_set(
    GapPal* pal, const GapWindowId windowId, const GapVector position) {
  (void)pal;
  (void)windowId;
  (void)position;
}

void gap_pal_window_clip_copy(GapPal* pal, const GapWindowId windowId, const String value) {
  (void)pal;
  (void)windowId;
  (void)value;
}

void gap_pal_window_clip_paste(GapPal* pal, const GapWindowId windowId) {
  (void)pal;
  (void)windowId;
}

String gap_pal_window_clip_paste_result(GapPal* pal, const GapWindowId windowId) {
  return pal_window(pal, windowId)->clipPaste;
}

String gap_pal_window_display_name(GapPal* pal, const GapWindowId windowId) {
  return pal_window(pal, windowId)->displayName;
}

f32 gap_pal_window_refresh_rate(GapPal* pal, const GapWindowId windowId) {
  return pal_window(pal, windowId)->refreshRate;
}

u16 gap_pal_window_dpi(GapPal* pal, const GapWindowId windowId) {
  return pal_window(pal, windowId)->dpi;
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

uptr gap_pal_native_app_handle(const GapPal* pal) { return (uptr)pal->wl.display; }

void gap_pal_modal_error(String message) { (void)message; }
