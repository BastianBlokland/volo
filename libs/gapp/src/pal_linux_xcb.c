#include "core_diag.h"
#include "log_logger.h"

#include "pal_internal.h"

#include <xcb/xcb.h>

struct sGAppPal {
  Allocator*        alloc;
  xcb_connection_t* xcbConnection;
  xcb_screen_t*     xcbScreen;
};

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

static void pal_xcb_check(GAppPal* pal) {
  const int error = xcb_connection_has_error(pal->xcbConnection);
  if (error) {
    diag_crash_msg(
        "xcb error: code {}, msg: '{}'", fmt_int(error), fmt_text(pal_xcb_err_str(error)));
  }
}

static void gapp_pal_xcb_connect(GAppPal* pal) {
  // Establish a connection with the x-server.
  int screen         = 0;
  pal->xcbConnection = xcb_connect(null, &screen);
  pal_xcb_check(pal);

  // Find the screen for our connection.
  const xcb_setup_t*    setup     = xcb_get_setup(pal->xcbConnection);
  xcb_screen_iterator_t screenItr = xcb_setup_roots_iterator(setup);
  for (int i = screen; i > 0; --i) {
    xcb_screen_next(&screenItr);
  }
  pal->xcbScreen = screenItr.data;

  log_i(
      "Xcb connected",
      log_param("status", fmt_int(setup->status)),
      log_param("protocol-major", fmt_int(setup->protocol_major_version)),
      log_param("protocol-minor", fmt_int(setup->protocol_minor_version)),
      log_param("screen-num", fmt_int(screen)),
      log_param("screen-width", fmt_int(pal->xcbScreen->width_in_pixels)),
      log_param("screen-height", fmt_int(pal->xcbScreen->height_in_pixels)));
}

static void gapp_pal_xcb_disconnect(GAppPal* pal) {
  xcb_disconnect(pal->xcbConnection);
  log_i("Xcb disconnected");
}

GAppPal* gapp_pal_create(Allocator* alloc) {
  GAppPal* pal = alloc_alloc_t(alloc, GAppPal);
  *pal         = (GAppPal){.alloc = alloc};

  gapp_pal_xcb_connect(pal);
  return pal;
}

void gapp_pal_destroy(GAppPal* pal) {
  gapp_pal_xcb_disconnect(pal);

  alloc_free_t(pal->alloc, pal);
}
