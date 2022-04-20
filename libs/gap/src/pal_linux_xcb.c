#include "core_array.h"
#include "core_diag.h"
#include "core_time.h"
#include "log_logger.h"

#include "pal_internal.h"

#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xfixes.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

/**
 * X11 client implementation using the xcb library.
 * Optionally uses the xkb, xfixes, icccm and cursor-util extensions.
 *
 * Standard: https://www.x.org/docs/ICCCM/icccm.pdf
 * Xcb: https://xcb.freedesktop.org/manual/
 */

#define pal_window_min_width 128
#define pal_window_min_height 128

/**
 * Utility to make synchronous xcb calls.
 */
#define pal_xcb_call(_CON_, _FUNC_, _ERR_, ...)                                                    \
  _FUNC_##_reply((_CON_), _FUNC_((_CON_), __VA_ARGS__), (_ERR_))

typedef enum {
  GapPalXcbExtFlags_Xkb        = 1 << 0,
  GapPalXcbExtFlags_XFixes     = 1 << 1,
  GapPalXcbExtFlags_Icccm      = 1 << 2,
  GapPalXcbExtFlags_CursorUtil = 1 << 3,
} GapPalXcbExtFlags;

typedef enum {
  GapPalFlags_CursorHidden = 1 << 0,
} GapPalFlags;

typedef struct {
  GapWindowId       id;
  GapVector         params[GapParam_Count];
  GapPalWindowFlags flags : 16;
  GapKeySet         keysPressed, keysPressedWithRepeat, keysReleased, keysDown;
  DynString         inputText;
  String            clipCopy, clipPaste;
} GapPalWindow;

struct sGapPal {
  Allocator* alloc;
  DynArray   windows; // GapPalWindow[]

  xcb_connection_t* xcbConnection;
  xcb_screen_t*     xcbScreen;
  GapPalXcbExtFlags extensions;
  usize             maxRequestLength;
  GapPalFlags       flags;

  struct xkb_context* xkbContext;
  i32                 xkbDeviceId;
  struct xkb_keymap*  xkbKeymap;
  struct xkb_state*   xkbState;

  xcb_cursor_context_t* cursorCtx;
  xcb_cursor_t          cursors[GapCursor_Count];

  xcb_atom_t atomProtoMsg, atomDeleteMsg, atomWmState, atomWmStateFullscreen,
      atomWmStateBypassCompositor, atomClipboard, atomVoloClipboard, atomTargets, atomUtf8String,
      atomPlainUtf8;
};

static const xcb_event_mask_t g_xcbWindowEventMask =
    XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
    XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
    XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE;

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

    string_maybe_free(g_alloc_heap, window->clipPaste);
    window->clipPaste = string_empty;
  }
}

static String pal_xcb_err_str(const int xcbErrCode) {
  switch (xcbErrCode) {
  case XCB_CONN_ERROR:
    return string_lit("Connection error");
  case XCB_CONN_CLOSED_EXT_NOTSUPPORTED:
    return string_lit("Extension not supported");
  case XCB_CONN_CLOSED_MEM_INSUFFICIENT:
    return string_lit("Insufficient memory available");
  case XCB_CONN_CLOSED_REQ_LEN_EXCEED:
    return string_lit("Request length exceeded");
  case XCB_CONN_CLOSED_PARSE_ERR:
    return string_lit("Failed to parse display string");
  case XCB_CONN_CLOSED_INVALID_SCREEN:
    return string_lit("No valid screen available");
  default:
    return string_lit("Unknown error");
  }
}

static GapKey pal_xcb_translate_key(const xcb_keycode_t key) {
  switch (key) {
  case 0x32: // Left-shift.
  case 0x3E: // Right-shift.
    return GapKey_Shift;
  case 0x25: // Left-control.
  case 0x69: // Right-control.
    return GapKey_Control;
  case 0x16:
    return GapKey_Backspace;
  case 0x77:
    return GapKey_Delete;
  case 0x17:
    return GapKey_Tab;
  case 0x31:
    return GapKey_Tilde;
  case 0x24:
    return GapKey_Return;
  case 0x9:
    return GapKey_Escape;
  case 0x41:
    return GapKey_Space;
  case 0x14:
  case 0x56: // Numpad +.
    return GapKey_Plus;
  case 0x15:
  case 0x52: // Numpad -.
    return GapKey_Minus;
  case 0x6E:
    return GapKey_Home;
  case 0x73:
    return GapKey_End;
  case 0x70:
    return GapKey_PageUp;
  case 0x75:
    return GapKey_PageDown;
  case 0x6F:
    return GapKey_ArrowUp;
  case 0x74:
    return GapKey_ArrowDown;
  case 0x72:
    return GapKey_ArrowRight;
  case 0x71:
    return GapKey_ArrowLeft;

  case 0x26:
    return GapKey_A;
  case 0x38:
    return GapKey_B;
  case 0x36:
    return GapKey_C;
  case 0x28:
    return GapKey_D;
  case 0x1A:
    return GapKey_E;
  case 0x29:
    return GapKey_F;
  case 0x2A:
    return GapKey_G;
  case 0x2B:
    return GapKey_H;
  case 0x1F:
    return GapKey_I;
  case 0x2C:
    return GapKey_J;
  case 0x2D:
    return GapKey_K;
  case 0x2E:
    return GapKey_L;
  case 0x3A:
    return GapKey_M;
  case 0x39:
    return GapKey_N;
  case 0x20:
    return GapKey_O;
  case 0x21:
    return GapKey_P;
  case 0x18:
    return GapKey_Q;
  case 0x1B:
    return GapKey_R;
  case 0x27:
    return GapKey_S;
  case 0x1C:
    return GapKey_T;
  case 0x1E:
    return GapKey_U;
  case 0x37:
    return GapKey_V;
  case 0x19:
    return GapKey_W;
  case 0x35:
    return GapKey_X;
  case 0x1D:
    return GapKey_Y;
  case 0x34:
    return GapKey_Z;

  case 0x13:
    return GapKey_Alpha0;
  case 0xA:
    return GapKey_Alpha1;
  case 0xB:
    return GapKey_Alpha2;
  case 0xC:
    return GapKey_Alpha3;
  case 0xD:
    return GapKey_Alpha4;
  case 0xE:
    return GapKey_Alpha5;
  case 0xF:
    return GapKey_Alpha6;
  case 0x10:
    return GapKey_Alpha7;
  case 0x11:
    return GapKey_Alpha8;
  case 0x12:
    return GapKey_Alpha9;

  case 0x43:
    return GapKey_F1;
  case 0x44:
    return GapKey_F2;
  case 0x45:
    return GapKey_F3;
  case 0x46:
    return GapKey_F4;
  case 0x47:
    return GapKey_F5;
  case 0x48:
    return GapKey_F6;
  case 0x49:
    return GapKey_F7;
  case 0x4A:
    return GapKey_F8;
  case 0x4B:
    return GapKey_F9;
  case 0x60:
    return GapKey_F10;
  case 0x5F:
    return GapKey_F11;
  case 0x4C:
    return GapKey_F12;
  }
  // log_d("Unrecognised xcb key", log_param("keycode", fmt_int(key, .base = 16)));
  return GapKey_None;
}

static void pal_xkb_log_callback(
    struct xkb_context* context, enum xkb_log_level level, const char* format, va_list args) {
  (void)context;
  Mem       buffer = mem_stack(256);
  const int chars  = vsnprintf(buffer.ptr, buffer.size, format, args);
  if (UNLIKELY(chars < 0)) {
    diag_crash_msg("vsnprintf() failed for xkb log message");
  }
  LogLevel logLevel;
  String   typeLabel;
  switch (level) {
  case XKB_LOG_LEVEL_CRITICAL:
  case XKB_LOG_LEVEL_ERROR:
  default:
    logLevel  = LogLevel_Error;
    typeLabel = string_lit("error");
    break;
  case XKB_LOG_LEVEL_WARNING:
    logLevel  = LogLevel_Warn;
    typeLabel = string_lit("warn");
    break;
  case XKB_LOG_LEVEL_INFO:
    logLevel  = LogLevel_Info;
    typeLabel = string_lit("info");
    break;
  case XKB_LOG_LEVEL_DEBUG:
    logLevel  = LogLevel_Debug;
    typeLabel = string_lit("debug");
    break;
  }
  log(g_logger,
      logLevel,
      "xkb {} log",
      log_param("type", fmt_text(typeLabel)),
      log_param("message", fmt_text(string_slice(buffer, 0, chars))));
}

static void pal_xcb_check_con(GapPal* pal) {
  const int error = xcb_connection_has_error(pal->xcbConnection);
  if (UNLIKELY(error)) {
    diag_crash_msg(
        "Xcb error: code {}, msg: '{}'", fmt_int(error), fmt_text(pal_xcb_err_str(error)));
  }
}

/**
 * Synchonously retrieve an xcb atom by name.
 * Xcb atoms are named tokens that are used in the x11 specification.
 */
static xcb_atom_t pal_xcb_atom(GapPal* pal, const String name) {
  xcb_generic_error_t*     err = null;
  xcb_intern_atom_reply_t* reply =
      pal_xcb_call(pal->xcbConnection, xcb_intern_atom, &err, 0, name.size, name.ptr);
  if (UNLIKELY(err)) {
    diag_crash_msg(
        "Xcb failed to retrieve atom: {}, err: {}", fmt_text(name), fmt_int(err->error_code));
  }
  const xcb_atom_t result = reply->atom;
  free(reply);
  return result;
}

static String pal_xcb_atom_name_scratch(GapPal* pal, const xcb_atom_t atom) {
  xcb_generic_error_t*       err = null;
  xcb_get_atom_name_reply_t* reply =
      pal_xcb_call(pal->xcbConnection, xcb_get_atom_name, &err, atom);
  if (UNLIKELY(err)) {
    diag_crash_msg("Xcb failed to retrieve atom name, err: {}", fmt_int(err->error_code));
  }
  const Mem name = mem_create(xcb_get_atom_name_name(reply), xcb_get_atom_name_name_length(reply));
  const String result = string_dup(g_alloc_scratch, name);
  free(reply);
  return result;
}

static void pal_xcb_connect(GapPal* pal) {
  // Establish a connection with the x-server.
  int screen         = 0;
  pal->xcbConnection = xcb_connect(null, &screen);
  pal_xcb_check_con(pal);
  pal->maxRequestLength = xcb_get_maximum_request_length(pal->xcbConnection) * 4;

  // Find the screen for our connection.
  const xcb_setup_t*    setup     = xcb_get_setup(pal->xcbConnection);
  xcb_screen_iterator_t screenItr = xcb_setup_roots_iterator(setup);
  for (int i = screen; i > 0; --i, xcb_screen_next(&screenItr))
    ;
  pal->xcbScreen = screenItr.data;

  // Retrieve atoms to use while communicating with the x-server.
  pal->atomProtoMsg                = pal_xcb_atom(pal, string_lit("WM_PROTOCOLS"));
  pal->atomDeleteMsg               = pal_xcb_atom(pal, string_lit("WM_DELETE_WINDOW"));
  pal->atomWmState                 = pal_xcb_atom(pal, string_lit("_NET_WM_STATE"));
  pal->atomWmStateFullscreen       = pal_xcb_atom(pal, string_lit("_NET_WM_STATE_FULLSCREEN"));
  pal->atomWmStateBypassCompositor = pal_xcb_atom(pal, string_lit("_NET_WM_BYPASS_COMPOSITOR"));
  pal->atomClipboard               = pal_xcb_atom(pal, string_lit("CLIPBOARD"));
  pal->atomVoloClipboard           = pal_xcb_atom(pal, string_lit("VOLO_CLIPBOARD"));
  pal->atomTargets                 = pal_xcb_atom(pal, string_lit("TARGETS"));
  pal->atomUtf8String              = pal_xcb_atom(pal, string_lit("UTF8_STRING"));
  pal->atomPlainUtf8               = pal_xcb_atom(pal, string_lit("text/plain;charset=utf-8"));

  MAYBE_UNUSED const GapVector screenSize =
      gap_vector(pal->xcbScreen->width_in_pixels, pal->xcbScreen->height_in_pixels);

  log_i(
      "Xcb connected",
      log_param("fd", fmt_int(xcb_get_file_descriptor(pal->xcbConnection))),
      log_param("max-req-length", fmt_size(pal->maxRequestLength)),
      log_param("screen-num", fmt_int(screen)),
      log_param("screen-size", gap_vector_fmt(screenSize)));
}

static void pal_xcb_wm_state_update(
    GapPal* pal, const GapWindowId windowId, const xcb_atom_t stateAtom, const bool active) {
  const xcb_client_message_event_t evt = {
      .response_type  = XCB_CLIENT_MESSAGE,
      .format         = sizeof(xcb_atom_t) * 8,
      .window         = (xcb_window_t)windowId,
      .type           = pal->atomWmState,
      .data.data32[0] = active ? 1 : 0,
      .data.data32[1] = stateAtom,
  };
  xcb_send_event(
      pal->xcbConnection,
      false,
      pal->xcbScreen->root,
      XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
      (const char*)&evt);
}

static void pal_xcb_bypass_compositor(GapPal* pal, const GapWindowId windowId, const bool active) {
  const u32 value = active ? 1 : 0;
  xcb_change_property(
      pal->xcbConnection,
      XCB_PROP_MODE_REPLACE,
      (xcb_window_t)windowId,
      pal->atomWmStateBypassCompositor,
      XCB_ATOM_CARDINAL,
      sizeof(u32) * 8,
      1,
      (const char*)&value);
}

static void pal_xkb_enable_flag(GapPal* pal, const xcb_xkb_per_client_flag_t flag) {
  xcb_xkb_per_client_flags_unchecked(
      pal->xcbConnection, XCB_XKB_ID_USE_CORE_KBD, flag, flag, 0, 0, 0);
}

/**
 * Initialize the xkb extension, gives us additional control over keyboard input.
 * More info: https://en.wikipedia.org/wiki/X_keyboard_extension
 */
static bool pal_xkb_init(GapPal* pal) {
  xcb_generic_error_t*           err   = null;
  xcb_xkb_use_extension_reply_t* reply = pal_xcb_call(
      pal->xcbConnection,
      xcb_xkb_use_extension,
      &err,
      XCB_XKB_MAJOR_VERSION,
      XCB_XKB_MINOR_VERSION);

  if (UNLIKELY(err)) {
    log_w("Failed to initialize the xkb extension", log_param("error", fmt_int(err->error_code)));
    free(reply);
    return false;
  }

  MAYBE_UNUSED const u16 versionMajor = reply->serverMajor;
  MAYBE_UNUSED const u16 versionMinor = reply->serverMinor;
  free(reply);

  pal->xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (UNLIKELY(!pal->xkbContext)) {
    log_w("Failed to initialize the xkb-common context");
    return false;
  }
  xkb_context_set_log_level(pal->xkbContext, XKB_LOG_LEVEL_INFO);
  xkb_context_set_log_fn(pal->xkbContext, pal_xkb_log_callback);
  pal->xkbDeviceId = xkb_x11_get_core_keyboard_device_id(pal->xcbConnection);
  if (UNLIKELY(pal->xkbDeviceId < 0)) {
    log_w("Failed to to retrieve the xkb keyboard device-id");
    return false;
  }
  pal->xkbKeymap = xkb_x11_keymap_new_from_device(
      pal->xkbContext, pal->xcbConnection, pal->xkbDeviceId, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!pal->xkbKeymap) {
    log_w("Failed to to retrieve the xkb keyboard keymap");
    return false;
  }
  pal->xkbState =
      xkb_x11_state_new_from_device(pal->xkbKeymap, pal->xcbConnection, pal->xkbDeviceId);
  if (!pal->xkbKeymap) {
    log_w("Failed to to retrieve the xkb keyboard state");
    return false;
  }

  const xkb_layout_index_t layoutCount   = xkb_keymap_num_layouts(pal->xkbKeymap);
  const char*              layoutNameRaw = xkb_keymap_layout_get_name(pal->xkbKeymap, 0);
  const String layoutName = layoutNameRaw ? string_from_null_term(layoutNameRaw) : string_empty;

  log_i(
      "Initialized xkb keyboard extension",
      log_param("version", fmt_list_lit(fmt_int(versionMajor), fmt_int(versionMinor))),
      log_param("device-id", fmt_int(pal->xkbDeviceId)),
      log_param("layout-count", fmt_int(layoutCount)),
      log_param("main-layout-name", fmt_text(layoutName)));
  return true;
}

/**
 * Initialize xfixes extension, contains various cursor visibility utilities.
 */
static bool pal_xfixes_init(GapPal* pal) {
  xcb_generic_error_t*              err   = null;
  xcb_xfixes_query_version_reply_t* reply = pal_xcb_call(
      pal->xcbConnection,
      xcb_xfixes_query_version,
      &err,
      XCB_XFIXES_MAJOR_VERSION,
      XCB_XFIXES_MINOR_VERSION);

  if (UNLIKELY(err)) {
    log_w(
        "Failed to initialize the xfixes extension", log_param("error", fmt_int(err->error_code)));
    free(reply);
    return false;
  }

  MAYBE_UNUSED const u32 versionMajor = reply->major_version;
  MAYBE_UNUSED const u32 versionMinor = reply->minor_version;
  free(reply);

  log_i(
      "Initialized xfixes extension",
      log_param("version", fmt_list_lit(fmt_int(versionMajor), fmt_int(versionMinor))));
  return true;
}

/**
 * Initialize the cursor-util extension.
 */
static bool pal_cursorutil_init(GapPal* pal) {
  const int res = xcb_cursor_context_new(pal->xcbConnection, pal->xcbScreen, &pal->cursorCtx);
  if (res != 0) {
    log_w("Failed to initialize the cursor-util xcb extension", log_param("error", fmt_int(res)));
    return false;
  }

  pal->cursors[GapCursor_Click]     = xcb_cursor_load_cursor(pal->cursorCtx, "hand1");
  pal->cursors[GapCursor_Text]      = xcb_cursor_load_cursor(pal->cursorCtx, "xterm");
  pal->cursors[GapCursor_Busy]      = xcb_cursor_load_cursor(pal->cursorCtx, "watch");
  pal->cursors[GapCursor_Crosshair] = xcb_cursor_load_cursor(pal->cursorCtx, "crosshair");

  log_i("Initialized cursor-util xcb extension");
  return true;
}

static void pal_init_extensions(GapPal* pal) {
  if (pal_xkb_init(pal)) {
    pal->extensions |= GapPalXcbExtFlags_Xkb;
  }
  if (pal_xfixes_init(pal)) {
    pal->extensions |= GapPalXcbExtFlags_XFixes;
  }
  pal->extensions |= GapPalXcbExtFlags_Icccm; // NOTE: No initialization is needed for ICCCM.
  if (pal_cursorutil_init(pal)) {
    pal->extensions |= GapPalXcbExtFlags_CursorUtil;
  }
}

static GapVector pal_query_cursor_pos(GapPal* pal, const GapWindowId windowId) {
  xcb_generic_error_t*       err = null;
  xcb_query_pointer_reply_t* reply =
      pal_xcb_call(pal->xcbConnection, xcb_query_pointer, &err, (xcb_window_t)windowId);

  if (UNLIKELY(err)) {
    log_w(
        "Failed to query the x11 cursor position",
        log_param("window-id", fmt_int(windowId)),
        log_param("error", fmt_int(err->error_code)));
    return gap_vector(0, 0);
  }
  return gap_vector(reply->win_x, reply->win_y);
}

static void
pal_set_window_min_size(GapPal* pal, const GapWindowId windowId, const GapVector minSize) {
  diag_assert(pal->extensions & GapPalXcbExtFlags_Icccm);

  xcb_size_hints_t hints = {0};
  xcb_icccm_size_hints_set_min_size(&hints, minSize.x, minSize.y);

  xcb_icccm_set_wm_size_hints(
      pal->xcbConnection, (xcb_window_t)windowId, XCB_ATOM_WM_NORMAL_HINTS, &hints);
}

static void pal_event_close(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (window) {
    window->flags |= GapPalWindowFlags_CloseRequested;
  }
}

static void pal_event_focus_gained(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || window->flags & GapPalWindowFlags_Focussed) {
    return;
  }
  window->flags |= GapPalWindowFlags_Focussed;
  window->flags |= GapPalWindowFlags_FocusGained;

  log_d("Window focus gained", log_param("id", fmt_int(windowId)));
}

static void pal_event_focus_lost(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || !(window->flags & GapPalWindowFlags_Focussed)) {
    return;
  }

  window->flags &= ~GapPalWindowFlags_Focussed;
  window->flags |= GapPalWindowFlags_FocusLost;

  gap_keyset_clear(&window->keysDown);

  log_d("Window focus lost", log_param("id", fmt_int(windowId)));
}

static void pal_event_resize(GapPal* pal, const GapWindowId windowId, const GapVector newSize) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || gap_vector_equal(window->params[GapParam_WindowSize], newSize)) {
    return;
  }
  window->params[GapParam_WindowSize] = newSize;
  window->flags |= GapPalWindowFlags_Resized;

  log_d(
      "Window resized",
      log_param("id", fmt_int(windowId)),
      log_param("size", gap_vector_fmt(newSize)));
}

static void pal_event_cursor(GapPal* pal, const GapWindowId windowId, const GapVector newPos) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || gap_vector_equal(window->params[GapParam_CursorPos], newPos)) {
    return;
  };

  /**
   * NOTE: Xcb uses top-left as the origin while the Volo project uses bottom-left, so we have to
   * remap the y coordinate.
   */

  window->params[GapParam_CursorPos] = (GapVector){
      .x = newPos.x,
      .y = window->params[GapParam_WindowSize].height - newPos.y,
  };
  window->flags |= GapPalWindowFlags_CursorMoved;
}

static void pal_event_press(GapPal* pal, const GapWindowId windowId, const GapKey key) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (window && key != GapKey_None) {
    gap_keyset_set(&window->keysPressedWithRepeat, key);
    if (!gap_keyset_test(&window->keysDown, key)) {
      gap_keyset_set(&window->keysPressed, key);
      gap_keyset_set(&window->keysDown, key);
    }
    window->flags |= GapPalWindowFlags_KeyPressed;
  }
}

static void pal_event_release(GapPal* pal, const GapWindowId windowId, const GapKey key) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (window && key != GapKey_None && gap_keyset_test(&window->keysDown, key)) {
    gap_keyset_set(&window->keysReleased, key);
    gap_keyset_unset(&window->keysDown, key);
    window->flags |= GapPalWindowFlags_KeyReleased;
  }
}

static void pal_event_text(GapPal* pal, const GapWindowId windowId, const xcb_keycode_t keyCode) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (UNLIKELY(!window)) {
    return;
  }
  if (pal->extensions & GapPalXcbExtFlags_Xkb) {
    char      buffer[32];
    const int textSize = xkb_state_key_get_utf8(pal->xkbState, keyCode, buffer, sizeof(buffer));
    dynstring_append(&window->inputText, mem_create(buffer, textSize));
  } else {
    /**
     * Xkb is not supported on this platform.
     * TODO: As a fallback we could implement a simple manual English ascii keymap.
     */
  }
}

static void pal_event_scroll(GapPal* pal, const GapWindowId windowId, const GapVector delta) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (window) {
    window->params[GapParam_ScrollDelta].x += delta.x;
    window->params[GapParam_ScrollDelta].y += delta.y;
    window->flags |= GapPalWindowFlags_Scrolled;
  }
}

static void pal_event_clip_copy_clear(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (window) {
    string_maybe_free(g_alloc_heap, window->clipCopy);
    window->clipCopy = string_empty;
  }
}

static void
pal_clip_send_targets(GapPal* pal, const xcb_window_t requestor, const xcb_atom_t property) {
  const xcb_atom_t targets[] = {
      pal->atomTargets,
      pal->atomUtf8String,
      pal->atomPlainUtf8,
  };
  xcb_change_property(
      pal->xcbConnection,
      XCB_PROP_MODE_REPLACE,
      requestor,
      property,
      XCB_ATOM_ATOM,
      sizeof(xcb_atom_t) * 8,
      array_elems(targets),
      (const char*)targets);
}

static void pal_clip_send_utf8(
    GapPal*             pal,
    const GapPalWindow* window,
    const xcb_window_t  requestor,
    const xcb_atom_t    property) {
  xcb_change_property(
      pal->xcbConnection,
      XCB_PROP_MODE_REPLACE,
      requestor,
      property,
      pal->atomUtf8String,
      sizeof(u8) * 8,
      (u32)window->clipCopy.size,
      window->clipCopy.ptr);
}

static void pal_event_clip_copy_request(
    GapPal* pal, const GapWindowId windowId, const xcb_selection_request_event_t* reqEvt) {

  xcb_selection_notify_event_t notifyEvt = {
      .response_type = XCB_SELECTION_NOTIFY,
      .time          = XCB_CURRENT_TIME,
      .requestor     = reqEvt->requestor,
      .selection     = reqEvt->selection,
      .target        = reqEvt->target,
  };

  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (window && reqEvt->selection == pal->atomClipboard && !string_is_empty(window->clipCopy)) {
    /**
     * Either return a collection of targers (think format types) of the clipboard data, or the data
     * itself as utf8.
     */
    if (reqEvt->target == pal->atomTargets) {
      pal_clip_send_targets(pal, reqEvt->requestor, reqEvt->property);
      notifyEvt.property = reqEvt->property;
    } else if (reqEvt->target == pal->atomUtf8String || reqEvt->target == pal->atomPlainUtf8) {
      pal_clip_send_utf8(pal, window, reqEvt->requestor, reqEvt->property);
      notifyEvt.property = reqEvt->property;
    } else {
      log_w(
          "Xcb copy request for unsupported target received",
          log_param("target", fmt_text(pal_xcb_atom_name_scratch(pal, reqEvt->target))));
    }
  }

  xcb_send_event(
      pal->xcbConnection, 0, reqEvt->requestor, XCB_EVENT_MASK_PROPERTY_CHANGE, (char*)&notifyEvt);
  xcb_flush(pal->xcbConnection);
}

static void pal_event_clip_paste_notify(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window) {
    return;
  }
  xcb_generic_error_t*      err   = null;
  xcb_get_property_reply_t* reply = pal_xcb_call(
      pal->xcbConnection,
      xcb_get_property,
      &err,
      0,
      (xcb_window_t)windowId,
      pal->atomVoloClipboard,
      XCB_ATOM_ANY,
      0,
      (u32)(pal->maxRequestLength / 4));
  if (UNLIKELY(err)) {
    diag_crash_msg("Xcb failed to retrieve clipboard value, err: {}", fmt_int(err->error_code));
  }

  string_maybe_free(g_alloc_heap, window->clipPaste);
  if (reply->value_len) {
    const String selectionMem = mem_create(xcb_get_property_value(reply), reply->value_len);
    window->clipPaste         = string_dup(g_alloc_heap, selectionMem);
    window->flags |= GapPalWindowFlags_ClipPaste;
  } else {
    window->clipPaste = string_empty;
  }
  free(reply);

  xcb_delete_property(pal->xcbConnection, (xcb_window_t)windowId, pal->atomVoloClipboard);
}

GapPal* gap_pal_create(Allocator* alloc) {
  GapPal* pal = alloc_alloc_t(alloc, GapPal);
  *pal        = (GapPal){.alloc = alloc, .windows = dynarray_create_t(alloc, GapPalWindow, 4)};

  pal_xcb_connect(pal);
  pal_init_extensions(pal);

  if (pal->extensions & GapPalXcbExtFlags_Xkb) {
    /**
     * Enable the 'detectableAutoRepeat' xkb flag.
     * By default x-server will send repeated press and release when holding a key, making it
     * impossible to detect 'true' presses and releases. This flag disables that behaviour.
     */
    pal_xkb_enable_flag(pal, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT);
  }

  xcb_flush(pal->xcbConnection);
  pal_xcb_check_con(pal);

  return pal;
}

void gap_pal_destroy(GapPal* pal) {
  while (pal->windows.size) {
    gap_pal_window_destroy(pal, dynarray_at_t(&pal->windows, 0, GapPalWindow)->id);
  }

  if (pal->xkbContext) {
    xkb_context_unref(pal->xkbContext);
  }
  if (pal->xkbKeymap) {
    xkb_keymap_unref(pal->xkbKeymap);
  }
  if (pal->xkbState) {
    xkb_state_unref(pal->xkbState);
  }
  array_for_t(pal->cursors, xcb_cursor_t, cursor) {
    if (*cursor != XCB_NONE) {
      xcb_free_cursor(pal->xcbConnection, *cursor);
    }
  }
  if (pal->cursorCtx) {
    xcb_cursor_context_free(pal->cursorCtx);
  }

  xcb_disconnect(pal->xcbConnection);
  log_i("Xcb disconnected");

  dynarray_destroy(&pal->windows);
  alloc_free_t(pal->alloc, pal);
}

void gap_pal_update(GapPal* pal) {
  // Clear volatile state, like the key-presses from the previous update.
  pal_clear_volatile(pal);

  // Handle all xcb events in the buffer.
  for (xcb_generic_event_t* evt; (evt = xcb_poll_for_event(pal->xcbConnection)); free(evt)) {
    switch (evt->response_type & ~0x80) {

    case 0: {
      const xcb_generic_error_t* errMsg = (const void*)evt;
      log_e("Xcb error", log_param("code", fmt_int(errMsg->error_code)));
    } break;

    case XCB_CLIENT_MESSAGE: {
      const xcb_client_message_event_t* clientMsg = (const void*)evt;
      if (clientMsg->data.data32[0] == pal->atomDeleteMsg) {
        pal_event_close(pal, clientMsg->window);
      }
    } break;

    case XCB_FOCUS_IN: {
      const xcb_focus_in_event_t* focusInMsg = (const void*)evt;
      pal_event_focus_gained(pal, focusInMsg->event);

      if (pal_maybe_window(pal, focusInMsg->event)) {
        // Update the cursor as it was probably moved since we where focussed last.
        pal_event_cursor(pal, focusInMsg->event, pal_query_cursor_pos(pal, focusInMsg->event));
      }
    } break;

    case XCB_FOCUS_OUT: {
      const xcb_focus_out_event_t* focusOutMsg = (const void*)evt;
      pal_event_focus_lost(pal, focusOutMsg->event);
    } break;

    case XCB_CONFIGURE_NOTIFY: {
      const xcb_configure_notify_event_t* configureMsg = (const void*)evt;
      const GapVector newSize = gap_vector(configureMsg->width, configureMsg->height);
      pal_event_resize(pal, configureMsg->window, newSize);
    } break;

    case XCB_MOTION_NOTIFY: {
      const xcb_motion_notify_event_t* motionMsg = (const void*)evt;
      const GapVector                  newPos = gap_vector(motionMsg->event_x, motionMsg->event_y);
      pal_event_cursor(pal, motionMsg->event, newPos);
    } break;

    case XCB_BUTTON_PRESS: {
      const xcb_button_press_event_t* pressMsg = (const void*)evt;
      switch (pressMsg->detail) {
      case XCB_BUTTON_INDEX_1:
        pal_event_press(pal, pressMsg->event, GapKey_MouseLeft);
        break;
      case XCB_BUTTON_INDEX_2:
        pal_event_press(pal, pressMsg->event, GapKey_MouseMiddle);
        break;
      case XCB_BUTTON_INDEX_3:
        pal_event_press(pal, pressMsg->event, GapKey_MouseRight);
        break;
      case XCB_BUTTON_INDEX_4: // Mouse-wheel scroll up.
        pal_event_scroll(pal, pressMsg->event, gap_vector(0, 1));
        break;
      case XCB_BUTTON_INDEX_5: // Mouse-wheel scroll down.
        pal_event_scroll(pal, pressMsg->event, gap_vector(0, -1));
        break;
      case 6: // XCB_BUTTON_INDEX_6 // Mouse-wheel scroll right.
        pal_event_scroll(pal, pressMsg->event, gap_vector(1, 0));
        break;
      case 7: // XCB_BUTTON_INDEX_7 // Mouse-wheel scroll left.
        pal_event_scroll(pal, pressMsg->event, gap_vector(-1, 0));
        break;
      }
    } break;

    case XCB_BUTTON_RELEASE: {
      const xcb_button_release_event_t* releaseMsg = (const void*)evt;
      switch (releaseMsg->detail) {
      case XCB_BUTTON_INDEX_1:
        pal_event_release(pal, releaseMsg->event, GapKey_MouseLeft);
        break;
      case XCB_BUTTON_INDEX_2:
        pal_event_release(pal, releaseMsg->event, GapKey_MouseMiddle);
        break;
      case XCB_BUTTON_INDEX_3:
        pal_event_release(pal, releaseMsg->event, GapKey_MouseRight);
        break;
      }
    } break;

    case XCB_KEY_PRESS: {
      const xcb_key_press_event_t* pressMsg = (const void*)evt;
      pal_event_press(pal, pressMsg->event, pal_xcb_translate_key(pressMsg->detail));
      if (pal->extensions & GapPalXcbExtFlags_Xkb) {
        xkb_state_update_key(pal->xkbState, pressMsg->detail, XKB_KEY_DOWN);
      }
      pal_event_text(pal, pressMsg->event, pressMsg->detail);
    } break;

    case XCB_KEY_RELEASE: {
      const xcb_key_release_event_t* releaseMsg = (const void*)evt;
      pal_event_release(pal, releaseMsg->event, pal_xcb_translate_key(releaseMsg->detail));
      if (pal->extensions & GapPalXcbExtFlags_Xkb) {
        xkb_state_update_key(pal->xkbState, releaseMsg->detail, XKB_KEY_UP);
      }
    } break;

    case XCB_SELECTION_CLEAR: {
      const xcb_selection_clear_event_t* selectionClearMsg = (const void*)evt;
      pal_event_clip_copy_clear(pal, selectionClearMsg->owner);
    } break;

    case XCB_SELECTION_REQUEST: {
      const xcb_selection_request_event_t* selectionRequestMsg = (const void*)evt;
      pal_event_clip_copy_request(pal, selectionRequestMsg->owner, selectionRequestMsg);
    } break;

    case XCB_SELECTION_NOTIFY: {
      const xcb_selection_notify_event_t* selectionNotifyMsg = (const void*)evt;
      if (selectionNotifyMsg->selection == pal->atomClipboard && selectionNotifyMsg->target) {
        pal_event_clip_paste_notify(pal, selectionNotifyMsg->requestor);
      }
    } break;
    }
  }
}

GapWindowId gap_pal_window_create(GapPal* pal, GapVector size) {
  xcb_connection_t* con = pal->xcbConnection;
  const GapWindowId id  = xcb_generate_id(con);

  if (size.width <= 0) {
    size.width = pal->xcbScreen->width_in_pixels;
  } else if (size.width < pal_window_min_width) {
    size.width = pal_window_min_width;
  }
  if (size.height <= 0) {
    size.height = pal->xcbScreen->height_in_pixels;
  } else if (size.height < pal_window_min_height) {
    size.height = pal_window_min_height;
  }

  const xcb_cw_t valuesMask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  const u32      values[2]  = {
            pal->xcbScreen->black_pixel,
            g_xcbWindowEventMask,
  };

  xcb_create_window(
      con,
      XCB_COPY_FROM_PARENT,
      (xcb_window_t)id,
      pal->xcbScreen->root,
      0,
      0,
      (u16)size.width,
      (u16)size.height,
      0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT,
      pal->xcbScreen->root_visual,
      valuesMask,
      values);

  // Register a custom delete message atom.
  xcb_change_property(
      con,
      XCB_PROP_MODE_REPLACE,
      (xcb_window_t)id,
      pal->atomProtoMsg,
      XCB_ATOM_ATOM,
      sizeof(xcb_atom_t) * 8,
      1,
      &pal->atomDeleteMsg);

  xcb_map_window(con, (xcb_window_t)id);
  pal_set_window_min_size(pal, id, gap_vector(pal_window_min_width, pal_window_min_height));
  xcb_flush(con);

  *dynarray_push_t(&pal->windows, GapPalWindow) = (GapPalWindow){
      .id                          = id,
      .params[GapParam_WindowSize] = size,
      .flags                       = GapPalWindowFlags_Focussed | GapPalWindowFlags_FocusGained,
      .inputText                   = dynstring_create(g_alloc_heap, 64),
  };

  log_i("Window created", log_param("id", fmt_int(id)), log_param("size", gap_vector_fmt(size)));

  return id;
}

void gap_pal_window_destroy(GapPal* pal, const GapWindowId windowId) {

  const xcb_void_cookie_t cookie =
      xcb_destroy_window_checked(pal->xcbConnection, (xcb_window_t)windowId);
  const xcb_generic_error_t* err = xcb_request_check(pal->xcbConnection, cookie);
  if (UNLIKELY(err)) {
    diag_crash_msg("xcb_destroy_window(), err: {}", fmt_int(err->error_code));
  }

  for (usize i = 0; i != pal->windows.size; ++i) {
    GapPalWindow* window = dynarray_at_t(&pal->windows, i, GapPalWindow);
    if (window->id == windowId) {
      dynstring_destroy(&window->inputText);
      string_maybe_free(g_alloc_heap, window->clipCopy);
      string_maybe_free(g_alloc_heap, window->clipPaste);
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
  const GapPalWindow* window = pal_window((GapPal*)pal, windowId);
  return dynstring_view(&window->inputText);
}

void gap_pal_window_title_set(GapPal* pal, const GapWindowId windowId, const String title) {
  const xcb_void_cookie_t cookie = xcb_change_property_checked(
      pal->xcbConnection,
      XCB_PROP_MODE_REPLACE,
      (xcb_window_t)windowId,
      XCB_ATOM_WM_NAME,
      pal->atomUtf8String,
      sizeof(u8) * 8,
      (u32)title.size,
      title.ptr);
  const xcb_generic_error_t* err = xcb_request_check(pal->xcbConnection, cookie);
  if (UNLIKELY(err)) {
    diag_crash_msg("xcb_change_property(), err: {}", fmt_int(err->error_code));
  }
}

void gap_pal_window_resize(
    GapPal* pal, const GapWindowId windowId, GapVector size, const bool fullscreen) {

  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  if (size.width <= 0) {
    size.width = pal->xcbScreen->width_in_pixels;
  } else if (size.width < pal_window_min_width) {
    size.width = pal_window_min_width;
  }

  if (size.height <= 0) {
    size.height = pal->xcbScreen->height_in_pixels;
  } else if (size.width < pal_window_min_height) {
    size.width = pal_window_min_height;
  }

  log_d(
      "Updating window size",
      log_param("id", fmt_int(windowId)),
      log_param("size", gap_vector_fmt(size)),
      log_param("fullscreen", fmt_bool(fullscreen)));

  if (fullscreen) {
    window->flags |= GapPalWindowFlags_Fullscreen;

    // TODO: Investigate supporting different sizes in fullscreen, this requires actually changing
    // the system display-adapter settings.
    pal_xcb_wm_state_update(pal, windowId, pal->atomWmStateFullscreen, true);
    pal_xcb_bypass_compositor(pal, windowId, true);
  } else {
    window->flags &= ~GapPalWindowFlags_Fullscreen;

    pal_xcb_wm_state_update(pal, windowId, pal->atomWmStateFullscreen, false);
    pal_xcb_bypass_compositor(pal, windowId, false);

    const u32 values[2] = {size.x, size.y};
    xcb_configure_window(
        pal->xcbConnection,
        (xcb_window_t)windowId,
        XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
        values);
  }

  xcb_flush(pal->xcbConnection);
  pal_xcb_check_con(pal);
}

void gap_pal_window_cursor_hide(GapPal* pal, const GapWindowId windowId, const bool hidden) {
  if (!(pal->extensions & GapPalXcbExtFlags_XFixes)) {
    log_w("Failed to update cursor visibility: XFixes extension not available");
    return;
  }

  if (hidden && !(pal->flags & GapPalFlags_CursorHidden)) {

    const xcb_void_cookie_t cookie =
        xcb_xfixes_hide_cursor_checked(pal->xcbConnection, (xcb_window_t)windowId);
    const xcb_generic_error_t* err = xcb_request_check(pal->xcbConnection, cookie);
    if (UNLIKELY(err)) {
      diag_crash_msg("xcb_xfixes_hide_cursor(), err: {}", fmt_int(err->error_code));
    }
    pal->flags |= GapPalFlags_CursorHidden;

  } else if (!hidden && pal->flags & GapPalFlags_CursorHidden) {

    const xcb_void_cookie_t cookie =
        xcb_xfixes_show_cursor_checked(pal->xcbConnection, (xcb_window_t)windowId);
    const xcb_generic_error_t* err = xcb_request_check(pal->xcbConnection, cookie);
    if (UNLIKELY(err)) {
      diag_crash_msg("xcb_xfixes_show_cursor(), err: {}", fmt_int(err->error_code));
    }
    pal->flags &= ~GapPalFlags_CursorHidden;
  }
}

void gap_pal_window_cursor_capture(GapPal* pal, const GapWindowId windowId, const bool captured) {
  /**
   * Not implemented for xcb.
   * In x11 you can still set the cursor position after the mouse leaves your window so in general
   * there isn't much need for this feature.
   */
  (void)pal;
  (void)windowId;
  (void)captured;
}

void gap_pal_window_cursor_set(GapPal* pal, const GapWindowId windowId, const GapCursor cursor) {
  if (pal->extensions & GapPalXcbExtFlags_CursorUtil) {
    xcb_change_window_attributes(
        pal->xcbConnection, (xcb_window_t)windowId, XCB_CW_CURSOR, &pal->cursors[cursor]);
  }
}

void gap_pal_window_cursor_pos_set(
    GapPal* pal, const GapWindowId windowId, const GapVector position) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  /**
   * NOTE: Xcb uses top-left as the origin while the Volo project uses bottom-left, so we have to
   * remap the y coordinate.
   */
  const GapVector xcbPos = {
      .x = position.x,
      .y = window->params[GapParam_WindowSize].height - position.y,
  };

  const xcb_void_cookie_t cookie = xcb_warp_pointer_checked(
      pal->xcbConnection, XCB_NONE, (xcb_window_t)windowId, 0, 0, 0, 0, xcbPos.x, xcbPos.y);
  const xcb_generic_error_t* err = xcb_request_check(pal->xcbConnection, cookie);
  if (UNLIKELY(err)) {
    diag_crash_msg("xcb_warp_pointer(), err: {}", fmt_int(err->error_code));
  }

  pal_window((GapPal*)pal, windowId)->params[GapParam_CursorPos] = position;
}

void gap_pal_window_clip_copy(GapPal* pal, const GapWindowId windowId, const String value) {
  const usize maxClipReqLen = pal->maxRequestLength - sizeof(xcb_change_property_request_t);
  if (value.size > maxClipReqLen) {
    // NOTE: Exceeding this limit would require splitting the data into chunks.
    log_w(
        "Clipboard copy request size exceeds limit",
        log_param("size", fmt_size(value.size)),
        log_param("limit", fmt_size(maxClipReqLen)));
    return;
  }

  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  string_maybe_free(g_alloc_heap, window->clipCopy);
  window->clipCopy = string_dup(g_alloc_heap, value);
  xcb_set_selection_owner(
      pal->xcbConnection, (xcb_window_t)windowId, pal->atomClipboard, XCB_CURRENT_TIME);

  xcb_flush(pal->xcbConnection);
  pal_xcb_check_con(pal);
}

void gap_pal_window_clip_paste(GapPal* pal, const GapWindowId windowId) {
  xcb_delete_property(pal->xcbConnection, (xcb_window_t)windowId, pal->atomVoloClipboard);
  xcb_convert_selection(
      pal->xcbConnection,
      (xcb_window_t)windowId,
      pal->atomClipboard,
      pal->atomUtf8String,
      pal->atomVoloClipboard,
      XCB_CURRENT_TIME);

  xcb_flush(pal->xcbConnection);
  pal_xcb_check_con(pal);
}

String gap_pal_window_clip_paste_result(GapPal* pal, const GapWindowId windowId) {
  return pal_maybe_window(pal, windowId)->clipPaste;
}

TimeDuration gap_pal_doubleclick_interval() {
  /**
   * Unfortunately x11 does not expose the concept of the system's 'double click time'.
   */
  return time_milliseconds(500);
}

GapNativeWm gap_pal_native_wm() { return GapNativeWm_Xcb; }

uptr gap_pal_native_app_handle(const GapPal* pal) { return (uptr)pal->xcbConnection; }
