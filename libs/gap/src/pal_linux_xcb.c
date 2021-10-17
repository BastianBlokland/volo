#include "core_diag.h"
#include "log_logger.h"

#include "pal_internal.h"

#include <stdlib.h>
#include <xcb/xcb.h>

typedef struct {
  GapWindowId id;
  u32         width, height;
} GapPalWindow;

struct sGapPal {
  Allocator* alloc;
  DynArray   windows; // GapPalWindow[]

  xcb_connection_t* xcbConnection;
  xcb_screen_t*     xcbScreen;

  xcb_atom_t xcbProtoMsgAtom;
  xcb_atom_t xcbDeleteMsgAtom;
  xcb_atom_t xcbWmStateAtom;
  xcb_atom_t xcbWmStateFullscreenAtom;
  xcb_atom_t xcbWmStateBypassCompositorAtom;
};

static const xcb_event_mask_t g_xcbWindowEventMask =
    XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
    XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;

// static GapPalWindow* pal_window(GapPal* pal, const GapWindowId id) {
//   dynarray_for_t(&pal->windows, GapPalWindow, window, {
//     if (window->id == id) {
//       return window;
//     }
//   });
//   return null;
// }

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
static xcb_atom_t pal_xcb_atom_sync(GapPal* pal, const String name) {
  /**
   * NOTE: An asynchronous version of this could be implemented by making all requests first and
   * then blocking using 'xcb_intern_atom_reply' only when we actually need the atom.
   */
  xcb_intern_atom_cookie_t cookie = xcb_intern_atom(pal->xcbConnection, 0, name.size, name.ptr);
  xcb_generic_error_t*     err    = null;
  xcb_intern_atom_reply_t* reply  = xcb_intern_atom_reply(pal->xcbConnection, cookie, &err);
  if (UNLIKELY(err)) {
    diag_crash_msg(
        "xcb failed to retrieve atom: {}, err: {}", fmt_text(name), fmt_int(err->error_code));
  }
  const xcb_atom_t result = reply->atom;
  free(reply); // TODO: Investigate if we can tell xcb to use our memory allocator.
  return result;
}

static void gap_pal_xcb_connect(GapPal* pal) {
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
  pal->xcbProtoMsgAtom          = pal_xcb_atom_sync(pal, string_lit("WM_PROTOCOLS"));
  pal->xcbDeleteMsgAtom         = pal_xcb_atom_sync(pal, string_lit("WM_DELETE_WINDOW"));
  pal->xcbWmStateAtom           = pal_xcb_atom_sync(pal, string_lit("_NET_WM_STATE"));
  pal->xcbWmStateFullscreenAtom = pal_xcb_atom_sync(pal, string_lit("_NET_WM_STATE_FULLSCREEN"));
  pal->xcbWmStateBypassCompositorAtom =
      pal_xcb_atom_sync(pal, string_lit("_NET_WM_BYPASS_COMPOSITOR"));

  log_i(
      "Xcb connected",
      log_param("fd", fmt_int(xcb_get_file_descriptor(pal->xcbConnection))),
      log_param("screen-num", fmt_int(screen)),
      log_param("screen-width", fmt_int(pal->xcbScreen->width_in_pixels)),
      log_param("screen-height", fmt_int(pal->xcbScreen->height_in_pixels)));
}

static void gap_pal_xcb_disconnect(GapPal* pal) {
  xcb_disconnect(pal->xcbConnection);
  log_i("Xcb disconnected");
}

GapPal* gap_pal_create(Allocator* alloc) {
  GapPal* pal = alloc_alloc_t(alloc, GapPal);
  *pal        = (GapPal){.alloc = alloc, .windows = dynarray_create_t(alloc, GapPalWindow, 4)};

  gap_pal_xcb_connect(pal);
  return pal;
}

void gap_pal_destroy(GapPal* pal) {
  while (pal->windows.size) {
    gap_pal_window_destroy(pal, dynarray_at_t(&pal->windows, 0, GapPalWindow)->id);
  }

  gap_pal_xcb_disconnect(pal);

  dynarray_destroy(&pal->windows);
  alloc_free_t(pal->alloc, pal);
}

GapWindowId gap_pal_window_create(GapPal* pal, u32 width, u32 height) {
  xcb_connection_t* con    = pal->xcbConnection;
  const GapWindowId window = xcb_generate_id(con);

  if (!width) {
    width = pal->xcbScreen->width_in_pixels;
  }
  if (!height) {
    height = pal->xcbScreen->height_in_pixels;
  }

  const xcb_cw_t valuesMask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  const u32      values[2]  = {
      pal->xcbScreen->black_pixel,
      g_xcbWindowEventMask,
  };

  xcb_create_window(
      con,
      XCB_COPY_FROM_PARENT,
      window,
      pal->xcbScreen->root,
      0,
      0,
      width,
      height,
      0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT,
      pal->xcbScreen->root_visual,
      valuesMask,
      values);

  // Register a custom delete message atom.
  xcb_change_property(
      con, XCB_PROP_MODE_REPLACE, window, pal->xcbProtoMsgAtom, 4, 32, 1, &pal->xcbDeleteMsgAtom);

  xcb_map_window(con, window);
  xcb_flush(con);

  *dynarray_push_t(&pal->windows, GapPalWindow) = (GapPalWindow){
      .id     = window,
      .width  = width,
      .height = height,
  };

  log_i(
      "Window created",
      log_param("id", fmt_int(window)),
      log_param("width", fmt_int(width)),
      log_param("height", fmt_int(height)));

  return window;
}

void gap_pal_window_destroy(GapPal* pal, const GapWindowId window) {

  xcb_destroy_window(pal->xcbConnection, window);
  xcb_flush(pal->xcbConnection);

  for (usize i = 0; i != pal->windows.size; ++i) {
    if (dynarray_at_t(&pal->windows, i, GapPalWindow)->id == window) {
      dynarray_remove_unordered(&pal->windows, i, 1);
      break;
    }
  }

  log_i("Window destroyed", log_param("id", fmt_int(window)));
}
