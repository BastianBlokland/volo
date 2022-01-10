#include "core_diag.h"
#include "log_logger.h"

#include "pal_internal.h"

#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xfixes.h>
#include <xcb/xkb.h>

#define pal_window_min_width 128
#define pal_window_min_height 128

/**
 * Utility to make synchronous xcb calls.
 */
#define pal_xcb_call(_CON_, _FUNC_, _ERR_, ...)                                                    \
  _FUNC_##_reply((_CON_), _FUNC_((_CON_), __VA_ARGS__), (_ERR_))

typedef enum {
  GapPalXcbExtFlags_Xkb    = 1 << 0,
  GapPalXcbExtFlags_XFixes = 1 << 1,
  GapPalXcbExtFlags_Icccm  = 1 << 2,
} GapPalXcbExtFlags;

typedef enum {
  GapPalFlags_CursorHidden = 1 << 0,
} GapPalFlags;

typedef struct {
  GapWindowId       id;
  GapVector         params[GapParam_Count];
  GapPalWindowFlags flags : 16;
  GapKeySet         keysPressed, keysReleased, keysDown;
} GapPalWindow;

struct sGapPal {
  Allocator* alloc;
  DynArray   windows; // GapPalWindow[]

  xcb_connection_t* xcbConnection;
  xcb_screen_t*     xcbScreen;
  GapPalXcbExtFlags extensions;
  GapPalFlags       flags;

  xcb_atom_t xcbProtoMsgAtom;
  xcb_atom_t xcbDeleteMsgAtom;
  xcb_atom_t xcbWmStateAtom;
  xcb_atom_t xcbWmStateFullscreenAtom;
  xcb_atom_t xcbWmStateBypassCompositorAtom;
};

static const xcb_event_mask_t g_xcbWindowEventMask =
    XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
    XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;

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
    gap_keyset_clear(&window->keysReleased);

    window->params[GapParam_ScrollDelta] = gap_vector(0, 0);

    window->flags &= ~GapPalWindowFlags_Volatile;
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
  case 0x32:
  case 0x3E:
    return GapKey_Shift;
  case 0x25:
  case 0x69:
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
    return GapKey_Plus;
  case 0x15:
    return GapKey_Minus;
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
  }
  // log_d("Unrecognised xcb key", log_param("keycode", fmt_int(key, .base = 16)));
  return GapKey_None;
}

static void pal_xcb_check_con(GapPal* pal) {
  const int error = xcb_connection_has_error(pal->xcbConnection);
  if (UNLIKELY(error)) {
    diag_crash_msg(
        "xcb error: code {}, msg: '{}'", fmt_int(error), fmt_text(pal_xcb_err_str(error)));
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
        "xcb failed to retrieve atom: {}, err: {}", fmt_text(name), fmt_int(err->error_code));
  }
  const xcb_atom_t result = reply->atom;
  free(reply);
  return result;
}

static void pal_xcb_connect(GapPal* pal) {
  // Establish a connection with the x-server.
  int screen         = 0;
  pal->xcbConnection = xcb_connect(null, &screen);
  pal_xcb_check_con(pal);

  // Find the screen for our connection.
  const xcb_setup_t*    setup     = xcb_get_setup(pal->xcbConnection);
  xcb_screen_iterator_t screenItr = xcb_setup_roots_iterator(setup);
  for (int i = screen; i > 0; --i, xcb_screen_next(&screenItr))
    ;
  pal->xcbScreen = screenItr.data;

  // Retreive atoms to use while communicating with the x-server.
  pal->xcbProtoMsgAtom                = pal_xcb_atom(pal, string_lit("WM_PROTOCOLS"));
  pal->xcbDeleteMsgAtom               = pal_xcb_atom(pal, string_lit("WM_DELETE_WINDOW"));
  pal->xcbWmStateAtom                 = pal_xcb_atom(pal, string_lit("_NET_WM_STATE"));
  pal->xcbWmStateFullscreenAtom       = pal_xcb_atom(pal, string_lit("_NET_WM_STATE_FULLSCREEN"));
  pal->xcbWmStateBypassCompositorAtom = pal_xcb_atom(pal, string_lit("_NET_WM_BYPASS_COMPOSITOR"));

  MAYBE_UNUSED const GapVector screenSize =
      gap_vector(pal->xcbScreen->width_in_pixels, pal->xcbScreen->height_in_pixels);

  log_i(
      "Xcb connected",
      log_param("fd", fmt_int(xcb_get_file_descriptor(pal->xcbConnection))),
      log_param("screen-num", fmt_int(screen)),
      log_param("screen-size", gap_vector_fmt(screenSize)));
}

static void pal_xcb_wm_state_update(
    GapPal* pal, const GapWindowId windowId, const xcb_atom_t stateAtom, const bool active) {
  const xcb_client_message_event_t evt = {
      .response_type  = XCB_CLIENT_MESSAGE,
      .format         = 32,
      .window         = (xcb_window_t)windowId,
      .type           = pal->xcbWmStateAtom,
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
  const u64 value = active ? 1 : 0;
  xcb_change_property(
      pal->xcbConnection,
      XCB_PROP_MODE_REPLACE,
      (xcb_window_t)windowId,
      pal->xcbWmStateBypassCompositorAtom,
      XCB_ATOM_CARDINAL,
      32,
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

  log_i(
      "Initialized xkb extension",
      log_param("version", fmt_list_lit(fmt_int(versionMajor), fmt_int(versionMinor))));
  return true;
}

/**
 * Initialize xfixes extension, contains various cursor utilities.
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

static void pal_init_extensions(GapPal* pal) {
  if (pal_xkb_init(pal)) {
    pal->extensions |= GapPalXcbExtFlags_Xkb;
  }
  if (pal_xfixes_init(pal)) {
    pal->extensions |= GapPalXcbExtFlags_XFixes;
  }
  pal->extensions |= GapPalXcbExtFlags_Icccm; // NOTE: No initialization is needed for ICCCM.
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

static void pal_event_resize(GapPal* pal, const GapWindowId windowId, const GapVector newSize) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || gap_vector_equal(window->params[GapParam_WindowSize], newSize)) {
    return;
  }
  window->params[GapParam_WindowSize] = newSize;
  window->flags |= GapPalWindowFlags_Resized;

  log_d("Window resized", log_param("size", gap_vector_fmt(newSize)));
}

static void pal_event_cursor(GapPal* pal, const GapWindowId windowId, const GapVector newPos) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || gap_vector_equal(window->params[GapParam_CursorPos], newPos)) {
    return;
  }
  window->params[GapParam_CursorPos] = newPos;
  window->flags |= GapPalWindowFlags_CursorMoved;
}

static void pal_event_press(GapPal* pal, const GapWindowId windowId, const GapKey key) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (window && key != GapKey_None && !gap_keyset_test(&window->keysDown, key)) {
    gap_keyset_set(&window->keysPressed, key);
    gap_keyset_set(&window->keysDown, key);
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

static void pal_event_scroll(GapPal* pal, const GapWindowId windowId, const GapVector delta) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (window) {
    window->params[GapParam_ScrollDelta].x += delta.x;
    window->params[GapParam_ScrollDelta].y += delta.y;
    window->flags |= GapPalWindowFlags_Scrolled;
  }
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

    case XCB_CLIENT_MESSAGE: {
      const xcb_client_message_event_t* clientMsg = (const void*)evt;
      if (clientMsg->data.data32[0] == pal->xcbDeleteMsgAtom) {
        pal_event_close(pal, clientMsg->window);
      }
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
    } break;

    case XCB_KEY_RELEASE: {
      const xcb_key_release_event_t* releaseMsg = (const void*)evt;
      pal_event_release(pal, releaseMsg->event, pal_xcb_translate_key(releaseMsg->detail));
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
      pal->xcbProtoMsgAtom,
      4,
      32,
      1,
      &pal->xcbDeleteMsgAtom);

  xcb_map_window(con, (xcb_window_t)id);
  pal_set_window_min_size(pal, id, gap_vector(pal_window_min_width, pal_window_min_height));
  xcb_flush(con);

  *dynarray_push_t(&pal->windows, GapPalWindow) = (GapPalWindow){
      .id                          = id,
      .params[GapParam_WindowSize] = size,
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
    if (dynarray_at_t(&pal->windows, i, GapPalWindow)->id == windowId) {
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

const GapKeySet* gap_pal_window_keys_released(const GapPal* pal, const GapWindowId windowId) {
  return &pal_window((GapPal*)pal, windowId)->keysReleased;
}

const GapKeySet* gap_pal_window_keys_down(const GapPal* pal, const GapWindowId windowId) {
  return &pal_window((GapPal*)pal, windowId)->keysDown;
}

void gap_pal_window_title_set(GapPal* pal, const GapWindowId windowId, const String title) {
  const xcb_void_cookie_t cookie = xcb_change_property_checked(
      pal->xcbConnection,
      XCB_PROP_MODE_REPLACE,
      (xcb_window_t)windowId,
      XCB_ATOM_WM_NAME,
      XCB_ATOM_STRING,
      8,
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
    pal_xcb_wm_state_update(pal, windowId, pal->xcbWmStateFullscreenAtom, true);
    pal_xcb_bypass_compositor(pal, windowId, true);
  } else {
    window->flags &= ~GapPalWindowFlags_Fullscreen;

    pal_xcb_wm_state_update(pal, windowId, pal->xcbWmStateFullscreenAtom, false);
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

void gap_pal_window_cursor_set(GapPal* pal, const GapWindowId windowId, GapVector position) {
  const xcb_void_cookie_t cookie = xcb_warp_pointer_checked(
      pal->xcbConnection, XCB_NONE, (xcb_window_t)windowId, 0, 0, 0, 0, position.x, position.y);
  const xcb_generic_error_t* err = xcb_request_check(pal->xcbConnection, cookie);
  if (UNLIKELY(err)) {
    diag_crash_msg("xcb_warp_pointer(), err: {}", fmt_int(err->error_code));
  }

  pal_window((GapPal*)pal, windowId)->params[GapParam_CursorPos] = position;
}

GapNativeWm gap_pal_native_wm() { return GapNativeWm_Xcb; }

uptr gap_pal_native_app_handle(const GapPal* pal) { return (uptr)pal->xcbConnection; }
