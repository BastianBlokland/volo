#include "core/alloc.h"
#include "core/diag.h"
#include "core/dynarray.h"
#include "core/dynlib.h"
#include "core/dynstring.h"
#include "log/logger.h"

#include "pal.h"
#include "wayland/wayland.h"

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

typedef struct {
  DynLib*             lib;
  WlFuncs             api;
  struct wl_display*  display;
  struct wl_registry* registry;

  struct wl_compositor* compositor;
  u32                   compositorVersion;
  struct xdg_wm_base*   xdgWmBase;
  u32                   xdgWmBaseVersion;

  struct wl_registry_listener registryListener;
  struct xdg_wm_base_listener xdgWmBaseListener;
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

  Wayland*             wl;
  struct wl_surface*   wlSurface;
  struct xdg_surface*  xdgSurface;
  struct xdg_toplevel* xdgToplevel;

  struct xdg_surface_listener  xdgSurfaceListener;
  struct xdg_toplevel_listener xdgToplevelListener;
} GapPalWindow;

struct sGapPal {
  Allocator* alloc;
  DynArray   windows; // GapPalWindow[]

  Wayland wl;
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

// -- Wayland listener callbacks --

static void wl_registry_global(
    void* data, struct wl_registry* registry, u32 name, const char* interface, u32 version) {
  Wayland* wl = data;
  if (string_eq(string_from_null_term(interface), string_lit("wl_compositor"))) {
    wl->compositor        = wlRegistryBind(&wl->api, registry, name, version, &wl_compositor_interface, 6);
    wl->compositorVersion = wl_compositor_get_version(&wl->api, wl->compositor);
  } else if (string_eq(string_from_null_term(interface), string_lit("xdg_wm_base"))) {
    wl->xdgWmBase        = wlRegistryBind(&wl->api, registry, name, version, &xdg_wm_base_interface, 1);
    wl->xdgWmBaseVersion = xdg_wm_base_get_version(&wl->api, wl->xdgWmBase);
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
  (void)states;
  if (width > 0 && height > 0) {
    window->params[GapParam_WindowSize]          = gap_vector(width, height);
    window->params[GapParam_WindowSizeRequested] = gap_vector(width, height);
    window->flags |= GapPalWindowFlags_Resized;
  }
}

static void xdg_toplevel_close(void* data, struct xdg_toplevel* top) {
  GapPalWindow* window = data;
  (void)top;
  window->flags |= GapPalWindowFlags_CloseRequested;
}

static void xdg_toplevel_noop_configure_bounds(
    void* data, struct xdg_toplevel* top, i32 w, i32 h) {
  (void)data;
  (void)top;
  (void)w;
  (void)h;
}

static void xdg_toplevel_noop_wm_capabilities(
    void* data, struct xdg_toplevel* top, struct wl_array* caps) {
  (void)data;
  (void)top;
  (void)caps;
}

// -- Initialization --

static bool pal_init_wl(Allocator* alloc, Wayland* out) {
  DynLibResult res =
      dynlib_load(alloc, string_lit("libwayland-client.so"), &out->lib);
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

static void pal_destroy_wl(Wayland* wl) {
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
  *pal = (GapPal){
      .alloc   = alloc,
      .windows = dynarray_create_t(alloc, GapPalWindow, 1),
      .wl      = wl,
  };
  return pal;
}

void gap_pal_destroy(GapPal* pal) {
  while (pal->windows.size) {
    gap_pal_window_destroy(pal, (GapWindowId)dynarray_at_t(&pal->windows, 0, GapPalWindow)->wlSurface);
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

  struct wl_surface*   wlSurface   = wl_compositor_create_surface(&wl->api, wl->compositor);
  struct xdg_surface*  xdgSurface  = xdg_wm_base_get_xdg_surface(&wl->api, wl->xdgWmBase, wlSurface);
  struct xdg_toplevel* xdgToplevel = xdg_surface_get_toplevel(&wl->api, xdgSurface);
  xdg_toplevel_set_app_id(&wl->api, xdgToplevel, "volo");

  GapPalWindow* window  = dynarray_push_t(&pal->windows, GapPalWindow);
  *window               = (GapPalWindow){
      .inputText   = dynstring_create(pal->alloc, 64),
      .wl          = wl,
      .wlSurface   = wlSurface,
      .xdgSurface  = xdgSurface,
      .xdgToplevel = xdgToplevel,
  };
  window->params[GapParam_WindowSize]          = size;
  window->params[GapParam_WindowSizeRequested] = size;

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
    Wayland* wl = &pal->wl;
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
  (void)pal;
  (void)windowId;
  (void)size;
  (void)fullscreen;
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
