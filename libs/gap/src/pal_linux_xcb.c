#include "core_array.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_dynstring.h"
#include "core_math.h"
#include "log_logger.h"

#include "pal_internal.h"

#include <xcb/xcb.h>

void SYS_DECL free(void*); // free from stdlib, xcb allocates various structures for us to free.

/**
 * X11 client implementation using the xcb library.
 * Optionally uses the xkbcommon, xfixes, randr and render extensions.
 *
 * Standard: https://www.x.org/docs/ICCCM/icccm.pdf
 * Xcb: https://xcb.freedesktop.org/manual/
 */

#define pal_window_min_width 128
#define pal_window_min_height 128
#define pal_window_default_refresh_rate 60.0f
#define pal_window_default_dpi 96

/**
 * Utility to make synchronous xcb calls.
 */
#define pal_xcb_call(_CON_, _FUNC_, _ERR_, ...)                                                    \
  _FUNC_##_reply((_CON_), _FUNC_((_CON_), __VA_ARGS__), (_ERR_))

#define pal_xcb_call_void(_CON_, _FUNC_, _ERR_) _FUNC_##_reply((_CON_), _FUNC_(_CON_), (_ERR_))

typedef unsigned int           XcbCookie;
typedef struct sXcbPictFormats XcbPictFormats;
typedef struct sXkbContext     XkbContext;
typedef struct sXkbKeyMap      XkbKeyMap;
typedef struct sXkbState       XkbState;
typedef u32                    XcbCursor;
typedef u32                    XcbDrawable;
typedef u32                    XcbPictFormat;
typedef u32                    XcbPicture;
typedef u32                    XcbRandrCrtc;
typedef u32                    XcbRandrMode;
typedef u32                    XcbRandrOutput;
typedef u32                    XkbKeycode;

typedef enum {
  XkbKeyDirection_Up,
  XkbKeyDirection_Down,
} XkbKeyDirection;

typedef struct {
  u16 redShift, redMask;
  u16 greenShift, greenMask;
  u16 blueShift, blueMask;
  u16 alphaShift, alphaMask;
} XcbDirectFormat;

typedef struct {
  XcbPictFormat   id;
  u8              type;
  u8              depth;
  u8              pad0[2];
  XcbDirectFormat direct;
  u32             colormap;
} XcbPictFormatInfo;

typedef struct {
  XcbPictFormatInfo* data;
  int                rem, index;
} XcbPictFormatInfoItr;

typedef struct {
  u8              responseType;
  u8              pad0;
  u16             sequence;
  u32             length;
  xcb_timestamp_t timestamp;
  xcb_timestamp_t configTimestamp;
  u16             numCrtcs;
  u16             numOutputs;
  u16             numModes;
  u16             namesLen;
  u8              pad1[8];
} XcbRandrScreenResources;

typedef struct {
  u8              responseType;
  u8              status;
  u16             sequence;
  u32             length;
  xcb_timestamp_t timestamp;
  XcbRandrCrtc    crtc;
  u32             mmWidth, mmHeight;
  u8              connection;
  u8              subpixelOrder;
  u16             numCrtcs, numModes, numPreferred, numClones, nameLen;
} XcbRandrOutputInfo;

typedef struct {
  u32 id;
  u16 width, height;
  u32 dotClock;
  u16 hsyncStart, hsyncEnd;
  u16 htotal;
  u16 hskew;
  u16 vsyncStart, vsyncEnd;
  u16 vtotal;
  u16 nameLen;
  u32 modeFlags;
} XcbRandrModeInfo;

typedef struct {
  XcbRandrModeInfo* data;
  int               rem;
  int               index;
} XcbRandrModeInfoIterator;

typedef struct {
  u8              responseType;
  u8              status;
  u16             sequence;
  u32             length;
  xcb_timestamp_t timestamp;
  i16             x, y;
  u16             width, height;
  XcbRandrMode    mode;
  u16             rotation;
  u16             rotations;
  u16             numOutputs, numPossibleOutputs;
} XcbRandrCrtcInfo;

typedef struct {
  u8              responseType;
  u8              rotation;
  u16             sequence;
  xcb_timestamp_t timestamp;
  xcb_timestamp_t configTimestamp;
  xcb_window_t    root, requestWindow;
  u16             sizeID;
  u16             subpixelOrder;
  u16             width, height;
  u16             mwidth, mheight;
} XcbRandrScreenChangeEvent;

typedef struct {
  DynLib* lib;
  // clang-format off
  xcb_connection_t*                         (SYS_DECL* connect)(const char* displayName, int* screenOut);
  void                                      (SYS_DECL* disconnect)(xcb_connection_t*);
  int                                       (SYS_DECL* flush)(xcb_connection_t*);
  u32                                       (SYS_DECL* get_maximum_request_length)(xcb_connection_t*);
  const struct xcb_setup_t*                 (SYS_DECL* get_setup)(xcb_connection_t*);
  xcb_screen_iterator_t                     (SYS_DECL* setup_roots_iterator)(const struct xcb_setup_t*);
  XcbCookie                                 (SYS_DECL* intern_atom)(xcb_connection_t*, u8 onlyIfExists, u16 nameLen, const char* name);
  xcb_intern_atom_reply_t*                  (SYS_DECL* intern_atom_reply)(xcb_connection_t*, XcbCookie, xcb_generic_error_t**);
  int                                       (SYS_DECL* get_file_descriptor)(xcb_connection_t*);
  int                                       (SYS_DECL* connection_has_error)(xcb_connection_t*);
  xcb_generic_event_t*                      (SYS_DECL* poll_for_event)(xcb_connection_t*);
  const struct xcb_query_extension_reply_t* (SYS_DECL* get_extension_data)(xcb_connection_t*, xcb_extension_t*);
  XcbCookie                                 (SYS_DECL* send_event)(xcb_connection_t*, u8 propagate, xcb_window_t destination, u32 eventMask, const char* event);
  XcbCookie                                 (SYS_DECL* convert_selection)(xcb_connection_t*, xcb_window_t requestor, xcb_atom_t selection, xcb_atom_t target, xcb_atom_t property, xcb_timestamp_t time);
  u32                                       (SYS_DECL* generate_id)(xcb_connection_t*);
  XcbCookie                                 (SYS_DECL* delete_property)(xcb_connection_t*, xcb_window_t, xcb_atom_t property);
  XcbCookie                                 (SYS_DECL* free_cursor)(xcb_connection_t*, xcb_cursor_t);
  XcbCookie                                 (SYS_DECL* free_gc)(xcb_connection_t*, xcb_gcontext_t);
  XcbCookie                                 (SYS_DECL* free_pixmap)(xcb_connection_t*, xcb_pixmap_t);
  XcbCookie                                 (SYS_DECL* put_image)(xcb_connection_t*, u8 format, xcb_drawable_t, xcb_gcontext_t, u16 width, u16 height, i16 dstX, i16 dstY, u8 leftPad, u8 depth, u32 dataLen, const u8* data);
  XcbCookie                                 (SYS_DECL* create_gc)(xcb_connection_t*, xcb_gcontext_t, xcb_drawable_t, u32 valueMask, const void* valueList);
  XcbCookie                                 (SYS_DECL* create_pixmap)(xcb_connection_t*, uint8_t depth, xcb_pixmap_t, xcb_drawable_t, uint16_t width, uint16_t height);
  // clang-format on
} Xcb;

typedef struct {
  DynLib* lib;
  // clang-format off
  int         (SYS_DECL* setup_xkb_extension)(xcb_connection_t*, u16 xkbMajor, u16 xkbMinor, i32 flags, u16* xkbMajorOut, u16* xkbMinorOut, u8* baseEventOut, u8* baseErrorOut);
  XkbContext* (SYS_DECL* context_new)(i32 flags);
  void        (SYS_DECL* context_unref)(XkbContext*);
  XcbCookie   (SYS_DECL* per_client_flags_unchecked)(xcb_connection_t*, u16 deviceSpec, u32 change, u32 value, u32 ctrlsToChange, u32 autoCtrls, u32 autoCtrlsValues);
  i32         (SYS_DECL* get_core_keyboard_device_id)(xcb_connection_t*);
  XkbKeyMap*  (SYS_DECL* keymap_new_from_device)(XkbContext*, xcb_connection_t*, i32 deviceId, i32 flags);
  void        (SYS_DECL* keymap_unref)(XkbKeyMap*);
  u32         (SYS_DECL* keymap_num_layouts)(XkbKeyMap*);
  const char* (SYS_DECL* keymap_layout_get_name)(XkbKeyMap*, u32 index);
  XkbState*   (SYS_DECL* state_new_from_device)(XkbKeyMap*, xcb_connection_t*, i32 deviceId);
  void        (SYS_DECL* state_unref)(XkbState*);
  i32         (SYS_DECL* state_key_get_utf8)(XkbState*, XkbKeycode, char* buffer, usize size);
  i32         (SYS_DECL* state_update_key)(XkbState*, XkbKeycode, XkbKeyDirection);
  // clang-format on
} XcbXkbCommon;

typedef struct {
  DynLib* lib;
  // clang-format off
  XcbCookie (SYS_DECL* query_version)(xcb_connection_t*, u32 majorVersion, u32 minorVersion);
  void*     (SYS_DECL* query_version_reply)(xcb_connection_t*, XcbCookie, xcb_generic_error_t**);
  XcbCookie (SYS_DECL* show_cursor)(xcb_connection_t*, xcb_window_t);
  XcbCookie (SYS_DECL* hide_cursor)(xcb_connection_t*, xcb_window_t);
  // clang-format on
} XcbXFixes;

typedef struct {
  DynLib*          lib;
  xcb_extension_t* id;
  // clang-format off
  XcbCookie                (SYS_DECL* query_version)(xcb_connection_t*, u32 majorVersion, u32 minorVersion);
  void*                    (SYS_DECL* query_version_reply)(xcb_connection_t*, XcbCookie, xcb_generic_error_t**);
  XcbCookie                (SYS_DECL* get_screen_resources_current)(xcb_connection_t*, xcb_window_t);
  XcbRandrScreenResources* (SYS_DECL* get_screen_resources_current_reply)(xcb_connection_t*, XcbCookie, xcb_generic_error_t**);
  XcbRandrOutput*          (SYS_DECL* get_screen_resources_current_outputs)(const XcbRandrScreenResources*);
  int                      (SYS_DECL* get_screen_resources_current_outputs_length)(const XcbRandrScreenResources*);
  XcbCookie                (SYS_DECL* get_output_info)(xcb_connection_t*, XcbRandrOutput, xcb_timestamp_t configTimestamp);
  XcbRandrOutputInfo*      (SYS_DECL* get_output_info_reply)(xcb_connection_t*, XcbCookie, xcb_generic_error_t**);
  u8*                      (SYS_DECL* get_output_info_name)(const XcbRandrOutputInfo*);
  int                      (SYS_DECL* get_output_info_name_length)(const XcbRandrOutputInfo*);
  XcbRandrModeInfoIterator (SYS_DECL* get_screen_resources_current_modes_iterator)(const XcbRandrScreenResources*);
  void                     (SYS_DECL* mode_info_next)(XcbRandrModeInfoIterator*);
  XcbCookie                (SYS_DECL* get_crtc_info)(xcb_connection_t*, XcbRandrCrtc, xcb_timestamp_t configTimestamp);
  XcbRandrCrtcInfo*        (SYS_DECL* get_crtc_info_reply)(xcb_connection_t*, XcbCookie, xcb_generic_error_t**);
  XcbCookie                (SYS_DECL* select_input)(xcb_connection_t*, xcb_window_t, u16 enable);
  // clang-format on
} XcbRandr;

typedef struct {
  DynLib*          lib;
  xcb_extension_t* id;
  // clang-format off
  XcbCookie            (SYS_DECL* query_version)(xcb_connection_t*, u32 majorVersion, u32 minorVersion);
  void*                (SYS_DECL* query_version_reply)(xcb_connection_t*, XcbCookie, xcb_generic_error_t**);
  XcbCookie            (SYS_DECL* query_pict_formats)(xcb_connection_t*);
  XcbPictFormats*      (SYS_DECL* query_pict_formats_reply)(xcb_connection_t*, XcbCookie, xcb_generic_error_t**);
  XcbPictFormatInfoItr (SYS_DECL* query_pict_formats_formats_iterator)(const XcbPictFormats*);
  void                 (SYS_DECL* pictforminfo_next)(XcbPictFormatInfoItr*);
  XcbCookie            (SYS_DECL* create_picture)(xcb_connection_t*, XcbPicture, XcbDrawable, XcbPictFormat, u32 valueMask, const void* valueList);
  XcbCookie            (SYS_DECL* create_cursor)(xcb_connection_t*, XcbCursor, XcbPicture, u16 x, u16 y);
  XcbCookie            (SYS_DECL* free_picture)(xcb_connection_t*, XcbPicture);
  // clang-format on
} XcbRender;

typedef enum {
  GapPalXcbExtFlags_Xkb    = 1 << 0,
  GapPalXcbExtFlags_XFixes = 1 << 1,
  GapPalXcbExtFlags_Randr  = 1 << 2,
  GapPalXcbExtFlags_Render = 1 << 3,
} GapPalXcbExtFlags;

typedef enum {
  GapPalFlags_CursorHidden   = 1 << 0,
  GapPalFlags_CursorConfined = 1 << 1,
} GapPalFlags;

typedef struct {
  GapWindowId       id;
  GapVector         params[GapParam_Count];
  GapVector         centerPos;
  GapPalWindowFlags flags : 16;
  GapIcon           icon : 8;
  GapCursor         cursor : 8;
  GapKeySet         keysPressed, keysPressedWithRepeat, keysReleased, keysDown;
  DynString         inputText;
  String            clipCopy, clipPaste;
  String            displayName;
  f32               refreshRate;
  u16               dpi;
} GapPalWindow;

typedef struct {
  String    name;
  GapVector position;
  GapVector size;
  f32       refreshRate;
  u16       dpi;
} GapPalDisplay;

struct sGapPal {
  Allocator* alloc;
  DynArray   windows;  // GapPalWindow[]
  DynArray   displays; // GapPalDisplay[]

  xcb_connection_t* xcbCon;
  xcb_screen_t*     xcbScreen;
  GapPalXcbExtFlags extensions;
  usize             maxRequestLength;
  u8                xkbFirstEvent, xkbFirstError;
  u8                randrFirstEvent;

  GapPalFlags flags;

  Xcb          xcb;
  XcbXkbCommon xkb;
  XcbXFixes    xfixes;
  XcbRandr     xrandr;
  XcbRender    xrender;

  XkbContext* xkbContext;
  i32         xkbDeviceId;
  XkbKeyMap*  xkbKeymap;
  XkbState*   xkbState;

  XcbPictFormat formatArgb32;

  Mem          icons[GapIcon_Count];
  xcb_cursor_t cursors[GapCursor_Count];

  xcb_atom_t atomProtoMsg, atomDeleteMsg, atomWmIcon, atomWmState, atomWmStateFullscreen,
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

static GapPalDisplay* pal_maybe_display(GapPal* pal, const GapVector position) {
  dynarray_for_t(&pal->displays, GapPalDisplay, display) {
    if (position.x < display->position.x) {
      continue;
    }
    if (position.y < display->position.y) {
      continue;
    }
    if (position.x >= display->position.x + display->size.width) {
      continue;
    }
    if (position.y >= display->position.y + display->size.height) {
      continue;
    }
    return display;
  }
  return null;
}

static void pal_clear_volatile(GapPal* pal) {
  dynarray_for_t(&pal->windows, GapPalWindow, window) {
    gap_keyset_clear(&window->keysPressed);
    gap_keyset_clear(&window->keysPressedWithRepeat);
    gap_keyset_clear(&window->keysReleased);

    window->params[GapParam_ScrollDelta] = gap_vector(0, 0);

    window->flags &= ~GapPalWindowFlags_Volatile;

    dynstring_clear(&window->inputText);

    string_maybe_free(g_allocHeap, window->clipPaste);
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
  case 0x40:
  case 0x6C:
    return GapKey_Alt;
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
  case 0x15:
  case 0x56: // Numpad +.
    return GapKey_Plus;
  case 0x14:
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
  case 0x22:
    return GapKey_BracketLeft;
  case 0x23:
    return GapKey_BracketRight;

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
  case 0x4C:
    return GapKey_F10;
  case 0x5F:
    return GapKey_F11;
  case 0x60:
    return GapKey_F12;
  }
  // log_d("Unrecognised xcb key", log_param("keycode", fmt_int(key, .base = 16)));
  return GapKey_None;
}

/**
 * Synchonously retrieve an xcb atom by name.
 * Xcb atoms are named tokens that are used in the x11 specification.
 */
static xcb_atom_t pal_xcb_atom(GapPal* pal, const String name) {
  xcb_generic_error_t*     err = null;
  xcb_intern_atom_reply_t* reply =
      pal_xcb_call(pal->xcbCon, pal->xcb.intern_atom, &err, 0, name.size, name.ptr);
  if (UNLIKELY(err)) {
    diag_crash_msg(
        "Xcb failed to retrieve atom: {}, err: {}", fmt_text(name), fmt_int(err->error_code));
  }
  const xcb_atom_t result = reply->atom;
  free(reply);
  return result;
}

static void pal_init_xcb(GapPal* pal, Xcb* out) {
  DynLibResult loadRes = dynlib_load(pal->alloc, string_lit("libxcb.so"), &out->lib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    diag_crash_msg("Failed to load Xcb ('libxcb.so'): {}", fmt_text(err));
  }

#define XCB_LOAD_SYM(_NAME_)                                                                       \
  do {                                                                                             \
    const String symName = string_lit("xcb_" #_NAME_);                                             \
    out->_NAME_          = dynlib_symbol(out->lib, symName);                                       \
    if (!out->_NAME_) {                                                                            \
      diag_crash_msg("Xcb symbol '{}' missing", fmt_text(symName));                                \
    }                                                                                              \
  } while (false)

  XCB_LOAD_SYM(connect);
  XCB_LOAD_SYM(disconnect);
  XCB_LOAD_SYM(flush);
  XCB_LOAD_SYM(get_maximum_request_length);
  XCB_LOAD_SYM(get_setup);
  XCB_LOAD_SYM(setup_roots_iterator);
  XCB_LOAD_SYM(intern_atom);
  XCB_LOAD_SYM(intern_atom_reply);
  XCB_LOAD_SYM(get_file_descriptor);
  XCB_LOAD_SYM(connection_has_error);
  XCB_LOAD_SYM(poll_for_event);
  XCB_LOAD_SYM(get_extension_data);
  XCB_LOAD_SYM(send_event);
  XCB_LOAD_SYM(convert_selection);
  XCB_LOAD_SYM(delete_property);
  XCB_LOAD_SYM(free_cursor);
  XCB_LOAD_SYM(free_gc);
  XCB_LOAD_SYM(free_pixmap);
  XCB_LOAD_SYM(put_image);
  XCB_LOAD_SYM(create_gc);
  XCB_LOAD_SYM(create_pixmap);

  /*

  xcb_query_pointer
  xcb_query_pointer_reply
  xcb_grab_pointer
  xcb_ungrab_pointer
  xcb_change_property
  xcb_get_property
  xcb_get_property_reply
  xcb_delete_property
  xcb_get_property_value
  xcb_change_window_attributes
  xcb_destroy_window
  xcb_configure_window
  xcb_create_window
  xcb_map_window
  xcb_warp_pointer
  xcb_set_selection_owner

  */

#undef XCB_LOAD_SYM

  // Establish a connection with the x-server.
  int screen            = 0;
  pal->xcbCon           = out->connect(null, &screen);
  pal->maxRequestLength = out->get_maximum_request_length(pal->xcbCon) * 4;

  // Find the screen for our connection.
  const xcb_setup_t*    setup     = out->get_setup(pal->xcbCon);
  xcb_screen_iterator_t screenItr = out->setup_roots_iterator(setup);
  if (!screenItr.data) {
    diag_crash_msg("Xcb no screen found");
  }
  pal->xcbScreen = screenItr.data;

  // Retrieve atoms to use while communicating with the x-server.
  pal->atomProtoMsg                = pal_xcb_atom(pal, string_lit("WM_PROTOCOLS"));
  pal->atomDeleteMsg               = pal_xcb_atom(pal, string_lit("WM_DELETE_WINDOW"));
  pal->atomWmIcon                  = pal_xcb_atom(pal, string_lit("_NET_WM_ICON"));
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
      log_param("fd", fmt_int(out->get_file_descriptor(pal->xcbCon))),
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
  pal->xcb.send_event(
      pal->xcbCon,
      false,
      pal->xcbScreen->root,
      XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
      (const char*)&evt);
}

static void pal_xcb_bypass_compositor(GapPal* pal, const GapWindowId windowId, const bool active) {
  const u32 value = active ? 1 : 0;
  xcb_change_property(
      pal->xcbCon,
      XCB_PROP_MODE_REPLACE,
      (xcb_window_t)windowId,
      pal->atomWmStateBypassCompositor,
      XCB_ATOM_CARDINAL,
      sizeof(u32) * 8,
      1,
      (const char*)&value);
}

static void pal_xcb_cursor_grab(GapPal* pal, const GapWindowId windowId) {
  xcb_grab_pointer(
      pal->xcbCon,
      true,
      (xcb_window_t)windowId,
      XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
      XCB_GRAB_MODE_ASYNC,
      XCB_GRAB_MODE_ASYNC,
      (xcb_window_t)windowId,
      XCB_NONE,
      XCB_CURRENT_TIME);
}

static void pal_xcb_cursor_grab_release(GapPal* pal) {
  xcb_ungrab_pointer(pal->xcbCon, XCB_CURRENT_TIME);
}

static void pal_xkb_enable_flag(GapPal* pal, const i32 flag) {
  enum { XCB_XKB_ID_USE_CORE_KBD = 256 };
  pal->xkb.per_client_flags_unchecked(pal->xcbCon, XCB_XKB_ID_USE_CORE_KBD, flag, flag, 0, 0, 0);
}

/**
 * Initialize the xkb extension, gives us additional control over keyboard input.
 * More info: https://en.wikipedia.org/wiki/X_keyboard_extension
 */
static bool pal_xkb_init(GapPal* pal, XcbXkbCommon* out) {
  DynLibResult loadRes = dynlib_load(pal->alloc, string_lit("libxkbcommon-x11.so"), &out->lib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load XkbCommon ('libxkbcommon-x11.so')", log_param("err", fmt_text(err)));
    return false;
  }

#define XKB_LOAD_SYM(_PREFIX_, _NAME_)                                                             \
  do {                                                                                             \
    const String symName = string_lit(#_PREFIX_ "_" #_NAME_);                                      \
    out->_NAME_          = dynlib_symbol(out->lib, symName);                                       \
    if (!out->_NAME_) {                                                                            \
      log_w("XkbCommon symbol '{}' missing", log_param("sym", fmt_text(symName)));                 \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  XKB_LOAD_SYM(xkb_x11, setup_xkb_extension);
  XKB_LOAD_SYM(xkb, context_new);
  XKB_LOAD_SYM(xkb, context_unref);
  XKB_LOAD_SYM(xcb_xkb, per_client_flags_unchecked);
  XKB_LOAD_SYM(xkb_x11, get_core_keyboard_device_id);
  XKB_LOAD_SYM(xkb_x11, keymap_new_from_device);
  XKB_LOAD_SYM(xkb, keymap_unref);
  XKB_LOAD_SYM(xkb, keymap_num_layouts);
  XKB_LOAD_SYM(xkb, keymap_layout_get_name);
  XKB_LOAD_SYM(xkb_x11, state_new_from_device);
  XKB_LOAD_SYM(xkb, state_unref);
  XKB_LOAD_SYM(xkb, state_key_get_utf8);
  XKB_LOAD_SYM(xkb, state_update_key);

#undef XKB_LOAD_SYM

  xcb_generic_error_t* err = null;

  u16       versionMajor;
  u16       versionMinor;
  const int setupRes = out->setup_xkb_extension(
      pal->xcbCon, 1, 0, 0, &versionMajor, &versionMinor, &pal->xkbFirstEvent, &pal->xkbFirstError);

  if (UNLIKELY(!setupRes)) {
    log_w("Xcb failed to initialize xkb", log_param("error", fmt_int(err->error_code)));
    return false;
  }

  pal->xkbContext = out->context_new(0);
  if (UNLIKELY(!pal->xkbContext)) {
    log_w("Xcb failed to create the xkb-common context");
    return false;
  }
  pal->xkbDeviceId = out->get_core_keyboard_device_id(pal->xcbCon);
  if (UNLIKELY(pal->xkbDeviceId < 0)) {
    log_w("Xcb failed to retrieve the xkb keyboard device-id");
    return false;
  }
  pal->xkbKeymap = out->keymap_new_from_device(pal->xkbContext, pal->xcbCon, pal->xkbDeviceId, 0);
  if (!pal->xkbKeymap) {
    log_w("Xcb failed to retrieve the xkb keyboard keymap");
    return false;
  }
  pal->xkbState = out->state_new_from_device(pal->xkbKeymap, pal->xcbCon, pal->xkbDeviceId);
  if (!pal->xkbKeymap) {
    log_w("Xcb failed to retrieve the xkb keyboard state");
    return false;
  }

  const u32    layoutCount   = out->keymap_num_layouts(pal->xkbKeymap);
  const char*  layoutNameRaw = out->keymap_layout_get_name(pal->xkbKeymap, 0);
  const String layoutName    = layoutNameRaw ? string_from_null_term(layoutNameRaw) : string_empty;

  log_i(
      "Xcb initialized XkbCommon",
      log_param("path", fmt_path(dynlib_path(out->lib))),
      log_param("version", fmt_list_lit(fmt_int(versionMajor), fmt_int(versionMinor))),
      log_param("device-id", fmt_int(pal->xkbDeviceId)),
      log_param("layout-count", fmt_int(layoutCount)),
      log_param("main-layout-name", fmt_text(layoutName)));
  return true;
}

/**
 * Initialize xfixes extension, contains various utilities.
 */
static bool pal_xfixes_init(GapPal* pal, XcbXFixes* out) {
  DynLibResult loadRes = dynlib_load(pal->alloc, string_lit("libxcb-xfixes.so"), &out->lib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load XFixes ('libxcb-xfixes.so')", log_param("err", fmt_text(err)));
    return false;
  }

#define XFIXES_LOAD_SYM(_NAME_)                                                                    \
  do {                                                                                             \
    const String symName = string_lit("xcb_xfixes_" #_NAME_);                                      \
    out->_NAME_          = dynlib_symbol(out->lib, symName);                                       \
    if (!out->_NAME_) {                                                                            \
      log_w("XFixes symbol '{}' missing", log_param("sym", fmt_text(symName)));                    \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  XFIXES_LOAD_SYM(query_version);
  XFIXES_LOAD_SYM(query_version_reply);
  XFIXES_LOAD_SYM(show_cursor);
  XFIXES_LOAD_SYM(hide_cursor);

#undef XFIXES_LOAD_SYM

  xcb_generic_error_t* err   = null;
  void*                reply = pal_xcb_call(pal->xcbCon, out->query_version, &err, 5, 0);
  free(reply);

  if (UNLIKELY(err)) {
    log_w("Failed to initialize XFixes", log_param("error", fmt_int(err->error_code)));
    return false;
  }

  log_i("Xcb initialized XFixes", log_param("path", fmt_path(dynlib_path(out->lib))));
  return true;
}

/**
 * Initialize the RandR extension.
 * More info: https://xcb.freedesktop.org/manual/group__XCB__RandR__API.html
 */
static bool pal_randr_init(GapPal* pal, XcbRandr* out, u8* firstEventOut) {
  DynLibResult loadRes = dynlib_load(pal->alloc, string_lit("libxcb-randr.so"), &out->lib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load XRandR ('libxcb-randr.so')", log_param("err", fmt_text(err)));
    return false;
  }

#define XRANDR_LOAD_SYM(_NAME_)                                                                    \
  do {                                                                                             \
    const String symName = string_lit("xcb_randr_" #_NAME_);                                       \
    out->_NAME_          = dynlib_symbol(out->lib, symName);                                       \
    if (!out->_NAME_) {                                                                            \
      log_w("XRandR symbol '{}' missing", log_param("sym", fmt_text(symName)));                    \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  XRANDR_LOAD_SYM(id);
  XRANDR_LOAD_SYM(query_version);
  XRANDR_LOAD_SYM(query_version_reply);
  XRANDR_LOAD_SYM(get_screen_resources_current);
  XRANDR_LOAD_SYM(get_screen_resources_current_reply);
  XRANDR_LOAD_SYM(get_screen_resources_current_outputs);
  XRANDR_LOAD_SYM(get_screen_resources_current_outputs_length);
  XRANDR_LOAD_SYM(get_output_info);
  XRANDR_LOAD_SYM(get_output_info_reply);
  XRANDR_LOAD_SYM(get_output_info_name);
  XRANDR_LOAD_SYM(get_output_info_name_length);
  XRANDR_LOAD_SYM(get_screen_resources_current_modes_iterator);
  XRANDR_LOAD_SYM(mode_info_next);
  XRANDR_LOAD_SYM(get_crtc_info);
  XRANDR_LOAD_SYM(get_crtc_info_reply);
  XRANDR_LOAD_SYM(select_input);

#undef XRANDR_LOAD_SYM

  const xcb_query_extension_reply_t* data = pal->xcb.get_extension_data(pal->xcbCon, out->id);
  if (!data || !data->present) {
    log_w("Xcb RandR extention not present");
    return false;
  }
  xcb_generic_error_t* err     = null;
  void*                version = pal_xcb_call(pal->xcbCon, out->query_version, &err, 1, 6);
  free(version);

  if (UNLIKELY(err)) {
    log_w("Failed to initialize XRandR", log_param("err", fmt_int(err->error_code)));
    return false;
  }

  *firstEventOut = data->first_event;
  log_i("Xcb initialized XRandR", log_param("path", fmt_path(dynlib_path(out->lib))));
  return true;
}

static bool pal_xrender_find_formats(GapPal* pal) {
  xcb_generic_error_t* err = null;
  XcbPictFormats* formats  = pal_xcb_call_void(pal->xcbCon, pal->xrender.query_pict_formats, &err);

  if (UNLIKELY(err)) {
    return false;
  }

  XcbPictFormatInfoItr itr = pal->xrender.query_pict_formats_formats_iterator(formats);
  for (; itr.rem; pal->xrender.pictforminfo_next(&itr)) {
    if (itr.data->depth != 32) {
      continue;
    }
    if (itr.data->type != 1 /* XCB_RENDER_PICT_TYPE_DIRECT */) {
      continue;
    }
    if (itr.data->direct.alphaShift != 0 || itr.data->direct.alphaMask != 0xFF) {
      continue;
    }
    if (itr.data->direct.redShift != 8 || itr.data->direct.redMask != 0xFF) {
      continue;
    }
    if (itr.data->direct.greenShift != 16 || itr.data->direct.greenMask != 0xFF) {
      continue;
    }
    if (itr.data->direct.blueShift != 24 || itr.data->direct.blueMask != 0xFF) {
      continue;
    }
    pal->formatArgb32 = itr.data->id;
    return true;
  }

  free(formats);
  return false; // Argb32 not found.
}

static bool pal_xrender_init(GapPal* pal, XcbRender* out) {
  DynLibResult loadRes = dynlib_load(pal->alloc, string_lit("libxcb-render.so"), &out->lib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load XRender ('libxcb-render.so')", log_param("err", fmt_text(err)));
    return false;
  }

#define XRENDER_LOAD_SYM(_NAME_)                                                                   \
  do {                                                                                             \
    const String symName = string_lit("xcb_render_" #_NAME_);                                      \
    out->_NAME_          = dynlib_symbol(out->lib, symName);                                       \
    if (!out->_NAME_) {                                                                            \
      log_w("Xcb-render symbol '{}' missing", log_param("sym", fmt_text(symName)));                \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  XRENDER_LOAD_SYM(id);
  XRENDER_LOAD_SYM(query_version);
  XRENDER_LOAD_SYM(query_version_reply);
  XRENDER_LOAD_SYM(query_pict_formats);
  XRENDER_LOAD_SYM(query_pict_formats_reply);
  XRENDER_LOAD_SYM(query_pict_formats_formats_iterator);
  XRENDER_LOAD_SYM(pictforminfo_next);
  XRENDER_LOAD_SYM(create_picture);
  XRENDER_LOAD_SYM(create_cursor);
  XRENDER_LOAD_SYM(free_picture);

#undef XRENDER_LOAD_SYM

  const xcb_query_extension_reply_t* data = pal->xcb.get_extension_data(pal->xcbCon, out->id);
  if (!data || !data->present) {
    log_w("Xcb XRender extention not present");
    return false;
  }
  xcb_generic_error_t* err     = null;
  void*                version = pal_xcb_call(pal->xcbCon, out->query_version, &err, 0, 11);
  free(version);

  if (UNLIKELY(err)) {
    log_w("Failed to initialize XRender extension", log_param("err", fmt_int(err->error_code)));
    return false;
  }
  if (!pal_xrender_find_formats(pal)) {
    log_w("Xcb failed to find required render formats");
    return false;
  }

  log_i("Xcb initialized XRender", log_param("path", fmt_path(dynlib_path(out->lib))));
  return true;
}

static void pal_init_extensions(GapPal* pal) {
  if (pal_xkb_init(pal, &pal->xkb)) {
    pal->extensions |= GapPalXcbExtFlags_Xkb;
  }
  if (pal_xfixes_init(pal, &pal->xfixes)) {
    pal->extensions |= GapPalXcbExtFlags_XFixes;
  }
  if (pal_randr_init(pal, &pal->xrandr, &pal->randrFirstEvent)) {
    pal->extensions |= GapPalXcbExtFlags_Randr;
  }
  if (pal_xrender_init(pal, &pal->xrender)) {
    pal->extensions |= GapPalXcbExtFlags_Render;
  }
}

static f32
pal_randr_refresh_rate(GapPal* pal, XcbRandrScreenResources* screen, const XcbRandrMode mode) {
  XcbRandrModeInfoIterator i = pal->xrandr.get_screen_resources_current_modes_iterator(screen);
  for (; i.rem; pal->xrandr.mode_info_next(&i)) {
    if (i.data->id != mode) {
      continue;
    }
    f64 verticalLines = i.data->vtotal;
    if (i.data->modeFlags & 32 /* XCB_RANDR_MODE_FLAG_DOUBLE_SCAN */) {
      verticalLines *= 2; // Double the number of lines.
    }
    if (i.data->modeFlags & 16 /* XCB_RANDR_MODE_FLAG_INTERLACE */) {
      verticalLines /= 2; // Interlace halves the number of lines.
    }
    if (i.data->htotal && verticalLines != 0.0) {
      return (f32)(((100 * (i64)i.data->dotClock) / (i.data->htotal * verticalLines)) / 100.0);
    }
    return pal_window_default_refresh_rate;
  }
  return pal_window_default_refresh_rate;
}

static void pal_randr_query_displays(GapPal* pal) {
  diag_assert(pal->extensions & GapPalXcbExtFlags_Randr);

  // Clear any previous queried displays.
  dynarray_for_t(&pal->displays, GapPalDisplay, d) { string_maybe_free(g_allocHeap, d->name); }
  dynarray_clear(&pal->displays);

  xcb_generic_error_t*     err    = null;
  XcbRandrScreenResources* screen = pal_xcb_call(
      pal->xcbCon, pal->xrandr.get_screen_resources_current, &err, pal->xcbScreen->root);
  if (UNLIKELY(err)) {
    diag_crash_msg("Xcb failed to retrieve RandR screen-info, err: {}", fmt_int(err->error_code));
  }

  const XcbRandrOutput* outputs = pal->xrandr.get_screen_resources_current_outputs(screen);
  const u32 numOutputs          = pal->xrandr.get_screen_resources_current_outputs_length(screen);
  for (u32 i = 0; i != numOutputs; ++i) {
    XcbRandrOutputInfo* output =
        pal_xcb_call(pal->xcbCon, pal->xrandr.get_output_info, &err, outputs[i], 0);
    if (UNLIKELY(err)) {
      diag_crash_msg("Xcb failed to retrieve RandR output-info, err: {}", fmt_int(err->error_code));
    }
    const String name = {
        .ptr  = pal->xrandr.get_output_info_name(output),
        .size = pal->xrandr.get_output_info_name_length(output),
    };

    if (output->crtc) {
      XcbRandrCrtcInfo* crtc =
          pal_xcb_call(pal->xcbCon, pal->xrandr.get_crtc_info, &err, output->crtc, 0);
      if (UNLIKELY(err)) {
        diag_crash_msg("Xcb failed to retrieve RandR crtc-info, err: {}", fmt_int(err->error_code));
      }
      const GapVector position       = gap_vector(crtc->x, crtc->y);
      const GapVector size           = gap_vector(crtc->width, crtc->height);
      const GapVector physicalSizeMm = gap_vector(output->mmWidth, output->mmHeight);
      const f32       refreshRate    = pal_randr_refresh_rate(pal, screen, crtc->mode);
      u16             dpi            = pal_window_default_dpi;
      if (output->mmWidth) {
        dpi = (u16)math_round_nearest_f32(crtc->width * 25.4f / physicalSizeMm.width);
      }

      log_i(
          "Xcb display found",
          log_param("name", fmt_text(name)),
          log_param("position", gap_vector_fmt(position)),
          log_param("size", gap_vector_fmt(size)),
          log_param("physical-size-mm", gap_vector_fmt(physicalSizeMm)),
          log_param("refresh-rate", fmt_float(refreshRate)),
          log_param("dpi", fmt_int(dpi)));

      *dynarray_push_t(&pal->displays, GapPalDisplay) = (GapPalDisplay){
          .name        = string_maybe_dup(g_allocHeap, name),
          .position    = position,
          .size        = size,
          .refreshRate = refreshRate,
          .dpi         = dpi,
      };
      free(crtc);
    }
    free(output);
  }
  free(screen);
}

static GapVector pal_query_cursor_pos(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window) {
    return gap_vector(0, 0);
  }

  GapVector                  result = gap_vector(0, 0);
  xcb_generic_error_t*       err    = null;
  xcb_query_pointer_reply_t* reply =
      pal_xcb_call(pal->xcbCon, xcb_query_pointer, &err, (xcb_window_t)windowId);

  if (UNLIKELY(err)) {
    log_w(
        "Xcb failed to query the x11 cursor position",
        log_param("window-id", fmt_int(windowId)),
        log_param("error", fmt_int(err->error_code)));
    goto Return;
  }

  // Xcb uses top-left as opposed to bottom-left, so we have to remap the y coordinate.
  result = (GapVector){
      .x = reply->win_x,
      .y = window->params[GapParam_WindowSize].height - reply->win_y,
  };

Return:
  free(reply);
  return result;
}

static void
pal_set_window_min_size(GapPal* pal, const GapWindowId windowId, const GapVector minSize) {
  // Needs to match 'WinXSizeHints' from the XServer.
  struct SizeHints {
    u32 flags;
    i32 x, y;
    i32 width, height;
    i32 minWidth, minHeight;
    i32 maxWidth, maxHeight;
    i32 width_inc, height_inc;
    i32 minAspectNum, minAspectDen;
    i32 maxAspectNum, maxAspectDen;
    i32 baseWidth, baseHeight;
    u32 winGravity;
  };

  const struct SizeHints newHints = {
      .flags     = 1 << 4 /* PMinSize */,
      .minWidth  = minSize.width,
      .minHeight = minSize.height,
  };

  xcb_change_property(
      pal->xcbCon,
      XCB_PROP_MODE_REPLACE,
      (xcb_window_t)windowId,
      XCB_ATOM_WM_NORMAL_HINTS,
      XCB_ATOM_WM_SIZE_HINTS,
      32,
      bytes_to_words(sizeof(newHints)),
      &newHints);
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

  if (pal->flags & GapPalFlags_CursorConfined) {
    pal_xcb_cursor_grab(pal, windowId);
  }

  log_d("Window focus gained", log_param("id", fmt_int(windowId)));
}

static void pal_event_focus_lost(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || !(window->flags & GapPalWindowFlags_Focussed)) {
    return;
  }

  window->flags &= ~GapPalWindowFlags_Focussed;
  window->flags |= GapPalWindowFlags_FocusLost;

  if (pal->flags & GapPalFlags_CursorConfined) {
    pal_xcb_cursor_grab_release(pal);
  }

  gap_keyset_clear(&window->keysDown);

  log_d("Window focus lost", log_param("id", fmt_int(windowId)));
}

static void pal_event_resize(
    GapPal* pal, const GapWindowId windowId, const GapVector newSize, const GapVector newCenter) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window) {
    return;
  }
  window->centerPos = newCenter;
  if (gap_vector_equal(window->params[GapParam_WindowSize], newSize)) {
    return;
  }
  window->params[GapParam_WindowSize] = newSize;
  window->flags |= GapPalWindowFlags_Resized;

  log_d(
      "Window resized",
      log_param("id", fmt_int(windowId)),
      log_param("size", gap_vector_fmt(newSize)));
}

static void pal_event_display_name_changed(
    GapPal* pal, const GapWindowId windowId, const String newDisplayName) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || string_eq(window->displayName, newDisplayName)) {
    return;
  }

  string_maybe_free(g_allocHeap, window->displayName);
  window->displayName = string_maybe_dup(g_allocHeap, newDisplayName);
  window->flags |= GapPalWindowFlags_DisplayNameChanged;

  log_d(
      "Window display-name changed",
      log_param("id", fmt_int(windowId)),
      log_param("display-name", fmt_text(newDisplayName)));
}

static void
pal_event_refresh_rate_changed(GapPal* pal, const GapWindowId windowId, const f32 newRefreshRate) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || window->refreshRate == newRefreshRate) {
    return;
  }
  window->refreshRate = newRefreshRate;
  window->flags |= GapPalWindowFlags_RefreshRateChanged;

  log_d(
      "Window refresh-rate changed",
      log_param("id", fmt_int(windowId)),
      log_param("refresh-rate", fmt_float(newRefreshRate)));
}

static void pal_event_dpi_changed(GapPal* pal, const GapWindowId windowId, const u16 newDpi) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || window->dpi == newDpi) {
    return;
  }
  window->dpi = newDpi;
  window->flags |= GapPalWindowFlags_DpiChanged;

  log_d(
      "Window dpi changed", log_param("id", fmt_int(windowId)), log_param("dpi", fmt_int(newDpi)));
}

static void pal_event_cursor(GapPal* pal, const GapWindowId windowId, const GapVector newPos) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window || gap_vector_equal(window->params[GapParam_CursorPos], newPos)) {
    return;
  };

  window->params[GapParam_CursorPos] = newPos;
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
    char      buff[32];
    const int textSize = pal->xkb.state_key_get_utf8(pal->xkbState, keyCode, buff, sizeof(buff));
    dynstring_append(&window->inputText, mem_create(buff, textSize));
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
    string_maybe_free(g_allocHeap, window->clipCopy);
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
      pal->xcbCon,
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
      pal->xcbCon,
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
     * Either return a collection of targets (think format types) of the clipboard data, or the data
     * itself as utf8.
     */
    if (reqEvt->target == pal->atomTargets) {
      pal_clip_send_targets(pal, reqEvt->requestor, reqEvt->property);
      notifyEvt.property = reqEvt->property;
    } else if (reqEvt->target == pal->atomUtf8String || reqEvt->target == pal->atomPlainUtf8) {
      pal_clip_send_utf8(pal, window, reqEvt->requestor, reqEvt->property);
      notifyEvt.property = reqEvt->property;
    } else {
      log_w("Xcb copy request for unsupported target received");
    }
  }

  pal->xcb.send_event(
      pal->xcbCon, 0, reqEvt->requestor, XCB_EVENT_MASK_PROPERTY_CHANGE, (char*)&notifyEvt);
}

static void pal_event_clip_paste_notify(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window) {
    return;
  }
  xcb_generic_error_t*      err   = null;
  xcb_get_property_reply_t* reply = pal_xcb_call(
      pal->xcbCon,
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

  string_maybe_free(g_allocHeap, window->clipPaste);
  if (reply->value_len) {
    const String selectionMem = mem_create(xcb_get_property_value(reply), reply->value_len);
    window->clipPaste         = string_dup(g_allocHeap, selectionMem);
    window->flags |= GapPalWindowFlags_ClipPaste;
  } else {
    window->clipPaste = string_empty;
  }
  free(reply);

  pal->xcb.delete_property(pal->xcbCon, (xcb_window_t)windowId, pal->atomVoloClipboard);
}

GapPal* gap_pal_create(Allocator* alloc) {
  GapPal* pal = alloc_alloc_t(alloc, GapPal);

  *pal = (GapPal){
      .alloc    = alloc,
      .windows  = dynarray_create_t(alloc, GapPalWindow, 4),
      .displays = dynarray_create_t(alloc, GapPalDisplay, 4),
  };

  pal_init_xcb(pal, &pal->xcb);
  pal_init_extensions(pal);

  if (pal->extensions & GapPalXcbExtFlags_Xkb) {
    /**
     * Enable the 'detectableAutoRepeat' xkb flag.
     * By default x-server will send repeated press and release when holding a key, making it
     * impossible to detect 'true' presses and releases. This flag disables that behavior.
     */
    pal_xkb_enable_flag(pal, 1 /* XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT */);
  }

  if (pal->extensions & GapPalXcbExtFlags_Randr) {
    pal_randr_query_displays(pal);
  }

  return pal;
}

void gap_pal_destroy(GapPal* pal) {
  while (pal->windows.size) {
    gap_pal_window_destroy(pal, dynarray_at_t(&pal->windows, 0, GapPalWindow)->id);
  }
  dynarray_for_t(&pal->displays, GapPalDisplay, d) { string_maybe_free(g_allocHeap, d->name); }

  if (pal->xkbContext) {
    pal->xkb.context_unref(pal->xkbContext);
  }
  if (pal->xkbKeymap) {
    pal->xkb.keymap_unref(pal->xkbKeymap);
  }
  if (pal->xkbState) {
    pal->xkb.state_unref(pal->xkbState);
  }

  dynlib_destroy(pal->xcb.lib);
  if (pal->xkb.lib) {
    dynlib_destroy(pal->xkb.lib);
  }
  if (pal->xfixes.lib) {
    dynlib_destroy(pal->xfixes.lib);
  }
  if (pal->xrandr.lib) {
    dynlib_destroy(pal->xrandr.lib);
  }
  if (pal->xrender.lib) {
    dynlib_destroy(pal->xrender.lib);
  }

  array_for_t(pal->icons, Mem, icon) { alloc_maybe_free(pal->alloc, *icon); }
  array_for_t(pal->cursors, xcb_cursor_t, cursor) {
    if (*cursor != XCB_NONE) {
      pal->xcb.free_cursor(pal->xcbCon, *cursor);
    }
  }

  pal->xcb.disconnect(pal->xcbCon);
  log_i("Xcb disconnected");

  dynarray_destroy(&pal->windows);
  dynarray_destroy(&pal->displays);
  alloc_free_t(pal->alloc, pal);
}

void gap_pal_update(GapPal* pal) {
  // Clear volatile state, like the key-presses from the previous update.
  pal_clear_volatile(pal);

  // Handle all xcb events in the buffer.
  for (xcb_generic_event_t* evt; (evt = pal->xcb.poll_for_event(pal->xcbCon)); free(evt)) {
    switch (evt->response_type & ~0x80) {

    case 0: {
      const xcb_generic_error_t* errMsg = (const void*)evt;
      log_e(
          "Xcb error",
          log_param("code", fmt_int(errMsg->error_code)),
          log_param("msg", fmt_text(pal_xcb_err_str(errMsg->error_code))));
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
      const GapVector newSize   = gap_vector(configureMsg->width, configureMsg->height);
      const GapVector newPos    = {configureMsg->x, configureMsg->y};
      const GapVector newCenter = {
          .x = newPos.x + newSize.width / 2,
          .y = newPos.y + newSize.height / 2,
      };
      pal_event_resize(pal, configureMsg->window, newSize, newCenter);

      const GapPalDisplay* display = pal_maybe_display(pal, newCenter);
      if (display) {
        pal_event_display_name_changed(pal, configureMsg->window, display->name);
        pal_event_refresh_rate_changed(pal, configureMsg->window, display->refreshRate);
        pal_event_dpi_changed(pal, configureMsg->window, display->dpi);
      }

      if (pal->flags & GapPalFlags_CursorConfined) {
        pal_xcb_cursor_grab(pal, configureMsg->window);
      }

      // Update the cursor position.
      pal_event_cursor(pal, configureMsg->window, pal_query_cursor_pos(pal, configureMsg->window));

    } break;

    case XCB_MOTION_NOTIFY: {
      const xcb_motion_notify_event_t* motionMsg = (const void*)evt;
      GapPalWindow*                    window    = pal_maybe_window(pal, motionMsg->event);
      if (window) {
        // Xcb uses top-left as opposed to bottom-left, so we have to remap the y coordinate.
        const GapVector newPos = {
            motionMsg->event_x,
            window->params[GapParam_WindowSize].height - motionMsg->event_y,
        };
        pal_event_cursor(pal, motionMsg->event, newPos);
      }
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
      case 8: // XCB_BUTTON_INDEX_8 // Extra mouse button (commonly the 'back' button).
        pal_event_press(pal, pressMsg->event, GapKey_MouseExtra1);
        break;
      case 9: // XCB_BUTTON_INDEX_9 // Extra mouse button (commonly the 'forward' button).
        pal_event_press(pal, pressMsg->event, GapKey_MouseExtra2);
        break;
      case 10: // XCB_BUTTON_INDEX_10 // Extra mouse button.
        pal_event_press(pal, pressMsg->event, GapKey_MouseExtra3);
        break;
      default:
        // log_d("Unrecognised xcb button", log_param("index", fmt_int(pressMsg->detail)));
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
      case 8: // XCB_BUTTON_INDEX_8 // Extra mouse button (commonly the 'back' button).
        pal_event_release(pal, releaseMsg->event, GapKey_MouseExtra1);
        break;
      case 9: // XCB_BUTTON_INDEX_9 // Extra mouse button (commonly the 'forward' button).
        pal_event_release(pal, releaseMsg->event, GapKey_MouseExtra2);
        break;
      case 10: // XCB_BUTTON_INDEX_10 // Extra mouse button.
        pal_event_release(pal, releaseMsg->event, GapKey_MouseExtra3);
        break;
      default:
        // log_d("Unrecognised xcb button", log_param("index", fmt_int(releaseMsg->detail)));
        break;
      }
    } break;

    case XCB_KEY_PRESS: {
      const xcb_key_press_event_t* pressMsg = (const void*)evt;
      pal_event_press(pal, pressMsg->event, pal_xcb_translate_key(pressMsg->detail));
      if (pal->extensions & GapPalXcbExtFlags_Xkb) {
        pal->xkb.state_update_key(pal->xkbState, pressMsg->detail, XkbKeyDirection_Down);
      }
      pal_event_text(pal, pressMsg->event, pressMsg->detail);
    } break;

    case XCB_KEY_RELEASE: {
      const xcb_key_release_event_t* releaseMsg = (const void*)evt;
      pal_event_release(pal, releaseMsg->event, pal_xcb_translate_key(releaseMsg->detail));
      if (pal->extensions & GapPalXcbExtFlags_Xkb) {
        pal->xkb.state_update_key(pal->xkbState, releaseMsg->detail, XkbKeyDirection_Up);
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
    default:
      if (pal->extensions & GapPalXcbExtFlags_Randr) {
        switch (evt->response_type - pal->randrFirstEvent) {
        case 0 /* XCB_RANDR_SCREEN_CHANGE_NOTIFY */: {
          const XcbRandrScreenChangeEvent* screenChangeMsg = (const void*)evt;

          log_d("Display change detected");
          pal_randr_query_displays(pal);

          const GapWindowId windowId = screenChangeMsg->requestWindow;
          GapPalWindow*     window   = pal_maybe_window(pal, windowId);
          if (window) {
            const GapPalDisplay* display = pal_maybe_display(pal, window->centerPos);
            if (display) {
              pal_event_display_name_changed(pal, windowId, display->name);
              pal_event_refresh_rate_changed(pal, windowId, display->refreshRate);
              pal_event_dpi_changed(pal, windowId, display->dpi);
            }
          }
        } break;
        }
      }
    }
  }
}

void gap_pal_flush(GapPal* pal) {
  pal->xcb.flush(pal->xcbCon);

  const int error = pal->xcb.connection_has_error(pal->xcbCon);
  if (UNLIKELY(error)) {
    diag_crash_msg(
        "Xcb error: code {}, msg: '{}'", fmt_int(error), fmt_text(pal_xcb_err_str(error)));
  }
}

static void gap_pal_icon_to_argb_flipped(const AssetIconComp* asset, const Mem out) {
  diag_assert(out.size == asset->width * asset->height * 4);
  const AssetIconPixel* inPixel = asset->pixelData.ptr;
  for (u32 y = asset->height; y-- != 0;) {
    for (u32 x = 0; x != asset->width; ++x) {
      u8* outPixel = bits_ptr_offset(out.ptr, (y * asset->width + x) * 4);
      outPixel[0]  = inPixel->a;
      outPixel[1]  = inPixel->r;
      outPixel[2]  = inPixel->g;
      outPixel[3]  = inPixel->b;
      ++inPixel;
    }
  }
}

static void gap_pal_icon_to_bgra_flipped(const AssetIconComp* asset, const Mem out) {
  diag_assert(out.size == asset->width * asset->height * 4);
  const AssetIconPixel* inPixel = asset->pixelData.ptr;
  for (u32 y = asset->height; y-- != 0;) {
    for (u32 x = 0; x != asset->width; ++x) {
      u8* outPixel = bits_ptr_offset(out.ptr, (y * asset->width + x) * 4);
      outPixel[0]  = inPixel->b;
      outPixel[1]  = inPixel->g;
      outPixel[2]  = inPixel->r;
      outPixel[3]  = inPixel->a;
      ++inPixel;
    }
  }
}

void gap_pal_icon_load(GapPal* pal, const GapIcon icon, const AssetIconComp* asset) {
  if (mem_valid(pal->icons[icon])) {
    alloc_free(pal->alloc, pal->icons[icon]);
  }

  /**
   * X11 icon data format:
   * - u32 width.
   * - u32 height.
   * - u8 pixelData[width * height * 4]. BGRA (ARGB little-endian) vertically flipped (top = y0).
   */

  pal->icons[icon] = alloc_alloc(pal->alloc, (asset->width * asset->height + 2) * sizeof(u32), 4);
  Mem dataRem      = pal->icons[icon];
  dataRem          = mem_write_le_u32(dataRem, asset->width);
  dataRem          = mem_write_le_u32(dataRem, asset->height);
  gap_pal_icon_to_bgra_flipped(asset, dataRem);

  // Update the icon for all existing windows that use this icon type.
  dynarray_for_t(&pal->windows, GapPalWindow, window) {
    if (window->icon == icon) {
      gap_pal_window_icon_set(pal, window->id, icon);
    }
  }
}

void gap_pal_cursor_load(GapPal* pal, const GapCursor id, const AssetIconComp* asset) {
  if (!(pal->extensions & GapPalXcbExtFlags_Render)) {
    return; // The render extension is required for pix-map cursors.
  }

  xcb_pixmap_t pixmap = pal->xcb.generate_id(pal->xcbCon);
  pal->xcb.create_pixmap(
      pal->xcbCon, 32, pixmap, pal->xcbScreen->root, asset->width, asset->height);

  XcbPicture picture = pal->xcb.generate_id(pal->xcbCon);
  pal->xrender.create_picture(pal->xcbCon, picture, pixmap, pal->formatArgb32, 0, null);

  xcb_gcontext_t graphicsContext = pal->xcb.generate_id(pal->xcbCon);
  pal->xcb.create_gc(pal->xcbCon, graphicsContext, pixmap, 0, null);

  Mem pixelBuffer = alloc_alloc(g_allocScratch, asset->width * asset->height * 4, 4);
  gap_pal_icon_to_argb_flipped(asset, pixelBuffer);

  pal->xcb.put_image(
      pal->xcbCon,
      XCB_IMAGE_FORMAT_Z_PIXMAP,
      pixmap,
      graphicsContext,
      asset->width,
      asset->height,
      0,
      0,
      0,
      32,
      (u32)pixelBuffer.size,
      pixelBuffer.ptr);

  pal->xcb.free_gc(pal->xcbCon, graphicsContext);

  xcb_cursor_t cursor = pal->xcb.generate_id(pal->xcbCon);
  pal->xrender.create_cursor(
      pal->xcbCon, cursor, picture, asset->hotspotX, asset->height - asset->hotspotY);

  pal->xrender.free_picture(pal->xcbCon, picture);
  pal->xcb.free_pixmap(pal->xcbCon, pixmap);

  if (pal->cursors[id] != XCB_NONE) {
    pal->xcb.free_cursor(pal->xcbCon, pal->cursors[id]);
  }
  pal->cursors[id] = cursor;

  // Update the cursor for any window that is currently using this cursor type.
  dynarray_for_t(&pal->windows, GapPalWindow, window) {
    if (window->cursor == id) {
      gap_pal_window_cursor_set(pal, window->id, id);
    }
  }
}

GapWindowId gap_pal_window_create(GapPal* pal, GapVector size) {
  xcb_connection_t* con = pal->xcbCon;
  const GapWindowId id  = pal->xcb.generate_id(con);

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

  const u32 values[2] = {
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

  *dynarray_push_t(&pal->windows, GapPalWindow) = (GapPalWindow){
      .id                          = id,
      .params[GapParam_WindowSize] = size,
      .flags                       = GapPalWindowFlags_Focussed | GapPalWindowFlags_FocusGained,
      .inputText                   = dynstring_create(g_allocHeap, 64),
      .refreshRate                 = pal_window_default_refresh_rate,
      .dpi                         = pal_window_default_dpi,
  };

  if (pal->extensions & GapPalXcbExtFlags_Randr) {
    const u16 mask = 1 /* XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE */;
    pal->xrandr.select_input(pal->xcbCon, (xcb_window_t)id, mask);
  }

  gap_pal_window_icon_set(pal, id, GapIcon_Main);
  pal_set_window_min_size(pal, id, gap_vector(pal_window_min_width, pal_window_min_height));
  xcb_map_window(con, (xcb_window_t)id);

  log_i("Window created", log_param("id", fmt_int(id)), log_param("size", gap_vector_fmt(size)));

  return id;
}

void gap_pal_window_destroy(GapPal* pal, const GapWindowId windowId) {

  xcb_destroy_window(pal->xcbCon, (xcb_window_t)windowId);

  for (usize i = 0; i != pal->windows.size; ++i) {
    GapPalWindow* window = dynarray_at_t(&pal->windows, i, GapPalWindow);
    if (window->id == windowId) {
      dynstring_destroy(&window->inputText);
      string_maybe_free(g_allocHeap, window->clipCopy);
      string_maybe_free(g_allocHeap, window->clipPaste);
      string_maybe_free(g_allocHeap, window->displayName);
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
  xcb_change_property(
      pal->xcbCon,
      XCB_PROP_MODE_REPLACE,
      (xcb_window_t)windowId,
      XCB_ATOM_WM_NAME,
      pal->atomUtf8String,
      sizeof(u8) * 8,
      (u32)title.size,
      title.ptr);
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
        pal->xcbCon,
        (xcb_window_t)windowId,
        XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
        values);
  }
}

void gap_pal_window_cursor_hide(GapPal* pal, const GapWindowId windowId, const bool hidden) {
  if (!(pal->extensions & GapPalXcbExtFlags_XFixes)) {
    log_w("Failed to update cursor visibility: XFixes extension not available");
    return;
  }

  if (hidden && !(pal->flags & GapPalFlags_CursorHidden)) {
    pal->xfixes.hide_cursor(pal->xcbCon, (xcb_window_t)windowId);
    pal->flags |= GapPalFlags_CursorHidden;

  } else if (!hidden && pal->flags & GapPalFlags_CursorHidden) {
    pal->xfixes.show_cursor(pal->xcbCon, (xcb_window_t)windowId);
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

void gap_pal_window_cursor_confine(GapPal* pal, const GapWindowId windowId, const bool confined) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);
  if (confined && !(pal->flags & GapPalFlags_CursorConfined)) {
    if (window->flags & GapPalWindowFlags_Focussed) {
      pal_xcb_cursor_grab(pal, windowId);
    }
    pal->flags |= GapPalFlags_CursorConfined;
    return;
  }
  if (!confined && (pal->flags & GapPalFlags_CursorConfined)) {
    if (window->flags & GapPalWindowFlags_Focussed) {
      pal_xcb_cursor_grab_release(pal);
    }
    pal->flags &= ~GapPalFlags_CursorConfined;
    return;
  }
}

void gap_pal_window_icon_set(GapPal* pal, const GapWindowId windowId, const GapIcon icon) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  if (mem_valid(pal->icons[icon])) {
    xcb_change_property(
        pal->xcbCon,
        XCB_PROP_MODE_REPLACE,
        (xcb_window_t)windowId,
        pal->atomWmIcon,
        XCB_ATOM_CARDINAL,
        sizeof(u32) * 8,
        (u32)(pal->icons[icon].size / sizeof(u32)),
        pal->icons[icon].ptr);
  } else {
    pal->xcb.delete_property(pal->xcbCon, (xcb_window_t)windowId, pal->atomWmIcon);
  }

  window->icon = icon;
}

void gap_pal_window_cursor_set(GapPal* pal, const GapWindowId windowId, const GapCursor cursor) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  xcb_change_window_attributes(
      pal->xcbCon, (xcb_window_t)windowId, XCB_CW_CURSOR, &pal->cursors[cursor]);

  window->cursor = cursor;
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
  xcb_warp_pointer(pal->xcbCon, XCB_NONE, (xcb_window_t)windowId, 0, 0, 0, 0, xcbPos.x, xcbPos.y);

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

  string_maybe_free(g_allocHeap, window->clipCopy);
  window->clipCopy = string_dup(g_allocHeap, value);
  xcb_set_selection_owner(
      pal->xcbCon, (xcb_window_t)windowId, pal->atomClipboard, XCB_CURRENT_TIME);
}

void gap_pal_window_clip_paste(GapPal* pal, const GapWindowId windowId) {
  pal->xcb.delete_property(pal->xcbCon, (xcb_window_t)windowId, pal->atomVoloClipboard);
  pal->xcb.convert_selection(
      pal->xcbCon,
      (xcb_window_t)windowId,
      pal->atomClipboard,
      pal->atomUtf8String,
      pal->atomVoloClipboard,
      XCB_CURRENT_TIME);
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
   * Unfortunately x11 does not expose the concept of the system's 'double click time'.
   */
  return time_milliseconds(500);
}

bool gap_pal_require_thread_affinity(void) {
  /**
   * There is no thread-affinity required for xcb, meaning we can call it from different threads.
   */
  return false;
}

GapNativeWm gap_pal_native_wm(void) { return GapNativeWm_Xcb; }

uptr gap_pal_native_app_handle(const GapPal* pal) { return (uptr)pal->xcbCon; }

void gap_pal_modal_error(GapPal* pal, const String message) {
  (void)pal;
  (void)message;

  // TODO: Implement.
}
