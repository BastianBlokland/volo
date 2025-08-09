#include "core_array.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_dynstring.h"
#include "core_math.h"
#include "log_logger.h"

#include "pal_internal.h"

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
#define xcb_call(_CON_, _FUNC_, _ERR_, ...)                                                        \
  _FUNC_##_reply((_CON_), _FUNC_((_CON_), __VA_ARGS__), (_ERR_))

#define xcb_call_void(_CON_, _FUNC_, _ERR_) _FUNC_##_reply((_CON_), _FUNC_(_CON_), (_ERR_))

typedef struct sXcbConnection  XcbConnection;
typedef struct sXcbPictFormats XcbPictFormats;
typedef struct sXcbSetup       XcbSetup;
typedef struct XcbExtension    XcbExtension;
typedef u32                    XcbAtom;
typedef u32                    XcbColormap;
typedef u32                    XcbCursor;
typedef u32                    XcbDrawable;
typedef u32                    XcbDrawable;
typedef u32                    XcbEventMask;
typedef u32                    XcbGcContext;
typedef u32                    XcbPictFormat;
typedef u32                    XcbPicture;
typedef u32                    XcbPixmap;
typedef u32                    XcbTimestamp;
typedef u32                    XcbVisualId;
typedef u32                    XcbWindow;
typedef u8                     XcbButton;
typedef unsigned int           XcbCookie;

typedef struct {
  XcbWindow   root;
  XcbColormap defaultColormap;
  u32         whitePixel, blackPixel;
  u32         currentInputMasks;
  u16         widthInPixels, heightInPixels;
  u16         widthInMillimeters, heightInMillimeters;
  u16         minInstalledMaps, maxInstalledMaps;
  XcbVisualId rootVisual;
  u8          backingStores;
  u8          saveUnders;
  u8          rootDepth;
  u8          allowedDepthsLen;
} XcbScreen;

typedef struct {
  u8  responseType, pad0;
  u16 sequence;
  u32 length;
  u8  present;
  u8  major_opcode;
  u8  first_event, first_error;
} XcbExtensionData;

typedef struct {
  u8      responseType, pad0;
  u16     sequence;
  u32     length;
  XcbAtom atom;
} XcbAtomData;

typedef struct {
  u8        responseType;
  u8        sameScreen;
  u16       sequence;
  u32       length;
  XcbWindow root, child;
  i16       rootX, rootY;
  i16       winX, winY;
  u16       mask;
  u8        pad0[2];
} XcbPointerData;

typedef struct {
  u8      responseType;
  u8      format;
  u16     sequence;
  u32     length;
  XcbAtom type;
  u32     bytesAfter;
  u32     valueLen;
  u8      pad0[12];
} XcbPropertyData;

typedef struct {
  u8  responseType, pad0;
  u16 sequence;
  u32 pad[7];
  u32 fullSequence;
} XcbGenericEvent;

typedef struct {
  u8  responseType;
  u8  errorCode;
  u16 sequence;
  u32 resourceId;
  u16 minorCode;
  u8  majorCode;
  u8  pad0;
  u32 pad[5];
  u32 fullSequence;
} XcbGenericError;

typedef struct {
  XcbScreen* data;
  int        rem, index;
} XcbScreenItr;

typedef struct {
  u8        responseType;
  u8        format;
  u16       sequence;
  XcbWindow window;
  XcbAtom   type;
  u32       data[5];
} XcbClientMessageEvent;

typedef struct {
  u8        responseType;
  u8        detail;
  u16       sequence;
  XcbWindow event;
  u8        mode;
  u8        pad0[3];
} XcbFocusEvent;

typedef struct {
  u8           responseType, pad0;
  u16          sequence;
  XcbTimestamp time;
  XcbWindow    owner;
  XcbWindow    requestor;
  XcbAtom      selection;
  XcbAtom      target;
  XcbAtom      property;
} XcbSelectionRequestEvent;

typedef struct {
  u8           responseType, pad0;
  u16          sequence;
  XcbTimestamp time;
  XcbWindow    requestor;
  XcbAtom      selection;
  XcbAtom      target;
  XcbAtom      property;
} XcbSelectionNotifyEvent;

typedef struct {
  u8        responseType, pad0;
  u16       sequence;
  XcbWindow event;
  XcbWindow window;
  XcbWindow aboveSibling;
  i16       x, y;
  u16       width, height;
  u16       borderWidth;
  u8        overrideRedirect, pad1;
} XcbConfigureNotifyEvent;

typedef struct {
  u8           responseType;
  u8           detail;
  u16          sequence;
  XcbTimestamp time;
  XcbWindow    root, event, child;
  i16          rootX, rootY;
  i16          eventX, eventY;
  u16          state;
  u8           sameScreen, pad0;
} XcbMotionNotifyEvent;

typedef struct {
  u8           responseType;
  XcbButton    detail;
  u16          sequence;
  XcbTimestamp time;
  XcbWindow    root, event, child;
  i16          rootX, rootY;
  i16          eventX, eventY;
  u16          state;
  u8           sameScreen, pad0;
} XcbButtonEvent;

typedef struct {
  u8           responseType;
  u8           detail;
  u16          sequence;
  XcbTimestamp time;
  XcbWindow    root, event, child;
  i16          rootX, rootY;
  i16          eventX, eventY;
  u16          state;
  u8           sameScreen, pad0;
} XcbKeyEvent;

typedef struct {
  u8           responseType, pad0;
  u16          sequence;
  XcbTimestamp time;
  XcbWindow    owner;
  XcbAtom      selection;
} XcbSelectionClearEvent;

typedef struct {
  u16 redShift, redMask;
  u16 greenShift, greenMask;
  u16 blueShift, blueMask;
  u16 alphaShift, alphaMask;
} XcbDirectFormat;

typedef struct {
  XcbPictFormat   id;
  u8              type, depth, pad0[2];
  XcbDirectFormat direct;
  u32             colormap;
} XcbPictFormatInfo;

typedef struct {
  XcbPictFormatInfo* data;
  int                rem, index;
} XcbPictFormatInfoItr;

typedef struct sXkbContext XkbContext;
typedef struct sXkbKeyMap  XkbKeyMap;
typedef struct sXkbState   XkbState;
typedef u32                XkbKeycode;
typedef u32                XkbStateComponent;
typedef u32                XkbModMask;
typedef u32                XkbLayoutIndex;

typedef struct {
  u8           responseType, xkbType;
  u16          sequence;
  XcbTimestamp time;
  u8           deviceId;
} XkbEventAny;

typedef struct {
  u8           responseType, xkbType;
  u16          sequence;
  XcbTimestamp time;
  u8           deviceId;
  u8           mods, baseMods, latchedMods, lockedMods;
  u8           group, baseGroup, latchedGroup, lockedGroup;
  u8           compatState;
  u8           grabMods, compatGrabMods;
  u8           lookupMods, compatLookupMods;
  u16          ptrBtnState;
  u16          changed;
  XkbKeycode   keycode;
  u8           eventType;
  u8           requestMajor, requestMinor;
} XkbEventStateNotify;

typedef u32 XRandrCrtc;
typedef u32 XRandrMode;
typedef u32 XRandrOutput;

typedef struct {
  u8           responseType, pad0;
  u16          sequence;
  u32          length;
  XcbTimestamp timestamp, configTimestamp;
  u16          numCrtcs;
  u16          numOutputs;
  u16          numModes;
  u16          namesLen;
  u8           pad1[8];
} XRandrScreenResources;

typedef struct {
  u8           responseType;
  u8           status;
  u16          sequence;
  u32          length;
  XcbTimestamp timestamp;
  XRandrCrtc   crtc;
  u32          mmWidth, mmHeight;
  u8           connection;
  u8           subpixelOrder;
  u16          numCrtcs, numModes, numPreferred, numClones, nameLen;
} XRandrOutputInfo;

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
} XRandrModeInfo;

typedef struct {
  XRandrModeInfo* data;
  int             rem;
  int             index;
} XRandrModeInfoIterator;

typedef struct {
  u8           responseType;
  u8           status;
  u16          sequence;
  u32          length;
  XcbTimestamp timestamp;
  i16          x, y;
  u16          width, height;
  XRandrMode   mode;
  u16          rotation;
  u16          rotations;
  u16          numOutputs, numPossibleOutputs;
} XRandrCrtcInfo;

typedef struct {
  u8           responseType;
  u8           rotation;
  u16          sequence;
  XcbTimestamp timestamp, configTimestamp;
  XcbWindow    root, requestWindow;
  u16          sizeID;
  u16          subpixelOrder;
  u16          width, height;
  u16          mwidth, mheight;
} XRandrScreenChangeEvent;

typedef struct {
  DynLib*        lib;
  XcbConnection* con;
  XcbScreen*     screen;
  usize          maxRequestLen;

  // clang-format off
  XcbAtom atomProtoMsg,
          atomDeleteMsg,
          atomWmIcon,
          atomWmState,
          atomWmStateFullscreen,
          atomWmStateBypassCompositor,
          atomClipboard,
          atomVoloClipboard,
          atomTargets,
          atomUtf8String,
          atomPlainUtf8;

  const XcbExtensionData* (SYS_DECL* get_extension_data)(XcbConnection*, XcbExtension*);
  const XcbSetup*         (SYS_DECL* get_setup)(XcbConnection*);
  int                     (SYS_DECL* connection_has_error)(XcbConnection*);
  XcbGenericError*        (SYS_DECL* request_check)(XcbConnection*, XcbCookie);
  int                     (SYS_DECL* flush)(XcbConnection*);
  int                     (SYS_DECL* get_file_descriptor)(XcbConnection*);
  u32                     (SYS_DECL* generate_id)(XcbConnection*);
  u32                     (SYS_DECL* get_maximum_request_length)(XcbConnection*);
  void                    (SYS_DECL* disconnect)(XcbConnection*);
  void*                   (SYS_DECL* get_property_value)(const XcbPropertyData*);
  XcbAtomData*            (SYS_DECL* intern_atom_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  XcbConnection*          (SYS_DECL* connect)(const char* displayName, int* screenOut);
  XcbCookie               (SYS_DECL* change_property)(XcbConnection*, u8 mode, XcbWindow window, XcbAtom property, XcbAtom type, u8 format, u32 dataLen, const void* data);
  XcbCookie               (SYS_DECL* change_window_attributes)(XcbConnection*, XcbWindow, u32 valueMask, const void* valueList);
  XcbCookie               (SYS_DECL* configure_window)(XcbConnection*, XcbWindow, u16 valueMask, const void* valueList);
  XcbCookie               (SYS_DECL* convert_selection)(XcbConnection*, XcbWindow requestor, XcbAtom selection, XcbAtom target, XcbAtom property, XcbTimestamp time);
  XcbCookie               (SYS_DECL* create_gc)(XcbConnection*, XcbGcContext, XcbDrawable, u32 valueMask, const void* valueList);
  XcbCookie               (SYS_DECL* create_pixmap)(XcbConnection*, u8 depth, XcbPixmap, XcbDrawable, u16 width, u16 height);
  XcbCookie               (SYS_DECL* create_window)(XcbConnection*, u8 depth, XcbWindow wid, XcbWindow parent, i16 x, i16 y, u16 width, u16 height, u16 borderWidth, u16 class, XcbVisualId, u32 valueMask, const void* valueList);
  XcbCookie               (SYS_DECL* delete_property)(XcbConnection*, XcbWindow, XcbAtom property);
  XcbCookie               (SYS_DECL* destroy_window)(XcbConnection*, XcbWindow);
  XcbCookie               (SYS_DECL* free_cursor)(XcbConnection*, XcbCursor);
  XcbCookie               (SYS_DECL* free_gc)(XcbConnection*, XcbGcContext);
  XcbCookie               (SYS_DECL* free_pixmap)(XcbConnection*, XcbPixmap);
  XcbCookie               (SYS_DECL* get_property)(XcbConnection*, u8 del, XcbWindow window, XcbAtom property, XcbAtom type, u32 longOffset, u32 longLength);
  XcbCookie               (SYS_DECL* grab_pointer)(XcbConnection*, u8 ownerEvents, XcbWindow grabWindow, u16 eventMask, u8 pointerMode, u8 keyboardMode, XcbWindow confineTo, XcbCursor cursor, XcbTimestamp);
  XcbCookie               (SYS_DECL* intern_atom)(XcbConnection*, u8 onlyIfExists, u16 nameLen, const char* name);
  XcbCookie               (SYS_DECL* map_window)(XcbConnection*, XcbWindow);
  XcbCookie               (SYS_DECL* put_image)(XcbConnection*, u8 format, XcbDrawable, XcbGcContext, u16 width, u16 height, i16 dstX, i16 dstY, u8 leftPad, u8 depth, u32 dataLen, const u8* data);
  XcbCookie               (SYS_DECL* query_pointer)(XcbConnection*, XcbWindow);
  XcbCookie               (SYS_DECL* send_event)(XcbConnection*, u8 propagate, XcbWindow destination, u32 eventMask, const char* event);
  XcbCookie               (SYS_DECL* set_selection_owner)(XcbConnection*, XcbWindow owner, XcbAtom selection, XcbTimestamp);
  XcbCookie               (SYS_DECL* ungrab_pointer)(XcbConnection*, XcbTimestamp);
  XcbCookie               (SYS_DECL* warp_pointer)(XcbConnection*, XcbWindow srcWindow, XcbWindow dstWindow, i16 srcX, i16 srcY, u16 srcWidth, u16 srcHeight, i16 dstX, i16 dstY);
  XcbGenericEvent*        (SYS_DECL* poll_for_event)(XcbConnection*);
  XcbPointerData*         (SYS_DECL* query_pointer_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  XcbPropertyData*        (SYS_DECL* get_property_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  XcbScreenItr            (SYS_DECL* setup_roots_iterator)(const XcbSetup*);
  // clang-format on
} Xcb;

typedef struct {
  DynLib*     lib;
  XkbContext* context;
  i32         deviceId;
  XkbKeyMap*  keymap;
  XkbState*   state;
  u8          firstEvent, firstError;

  // clang-format off
  XcbCookie         (SYS_DECL* select_events_checked)(XcbConnection*, u16 deviceSpec, u16 affectWhich, u16 clear, u16 selectAll, u16 affectMap, u16 map, const void* details);
  const char*       (SYS_DECL* keymap_layout_get_name)(XkbKeyMap*, u32 index);
  i32               (SYS_DECL* get_core_keyboard_device_id)(XcbConnection*);
  i32               (SYS_DECL* state_key_get_utf8)(XkbState*, XkbKeycode, char* buffer, usize size);
  XkbStateComponent (SYS_DECL* state_update_mask)(XkbState*, XkbModMask depressed, XkbModMask latched, XkbModMask locked, XkbLayoutIndex depressedLayout, XkbLayoutIndex latchedLayout, XkbLayoutIndex lockedLayout);
  int               (SYS_DECL* setup_xkb_extension)(XcbConnection*, u16 xkbMajor, u16 xkbMinor, i32 flags, u16* xkbMajorOut, u16* xkbMinorOut, u8* baseEventOut, u8* baseErrorOut);
  u32               (SYS_DECL* keymap_num_layouts)(XkbKeyMap*);
  void              (SYS_DECL* context_unref)(XkbContext*);
  void              (SYS_DECL* keymap_unref)(XkbKeyMap*);
  void              (SYS_DECL* state_unref)(XkbState*);
  XcbCookie         (SYS_DECL* per_client_flags_unchecked)(XcbConnection*, u16 deviceSpec, u32 change, u32 value, u32 ctrlsToChange, u32 autoCtrls, u32 autoCtrlsValues);
  XkbContext*       (SYS_DECL* context_new)(i32 flags);
  XkbKeyMap*        (SYS_DECL* keymap_new_from_device)(XkbContext*, XcbConnection*, i32 deviceId, i32 flags);
  XkbState*         (SYS_DECL* state_new_from_device)(XkbKeyMap*, XcbConnection*, i32 deviceId);
  // clang-format on
} XkbCommon;

typedef struct {
  DynLib* lib;
  // clang-format off
  void*     (SYS_DECL* query_version_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  XcbCookie (SYS_DECL* hide_cursor)(XcbConnection*, XcbWindow);
  XcbCookie (SYS_DECL* query_version)(XcbConnection*, u32 majorVersion, u32 minorVersion);
  XcbCookie (SYS_DECL* show_cursor)(XcbConnection*, XcbWindow);
  // clang-format on
} XFixes;

typedef struct {
  DynLib*       lib;
  XcbExtension* id;
  u8            firstEvent;

  // clang-format off
  int                    (SYS_DECL* get_output_info_name_length)(const XRandrOutputInfo*);
  int                    (SYS_DECL* get_screen_resources_current_outputs_length)(const XRandrScreenResources*);
  u8*                    (SYS_DECL* get_output_info_name)(const XRandrOutputInfo*);
  void                   (SYS_DECL* mode_info_next)(XRandrModeInfoIterator*);
  void*                  (SYS_DECL* query_version_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  XcbCookie              (SYS_DECL* get_crtc_info)(XcbConnection*, XRandrCrtc, XcbTimestamp configTimestamp);
  XcbCookie              (SYS_DECL* get_output_info)(XcbConnection*, XRandrOutput, XcbTimestamp configTimestamp);
  XcbCookie              (SYS_DECL* get_screen_resources_current)(XcbConnection*, XcbWindow);
  XcbCookie              (SYS_DECL* query_version)(XcbConnection*, u32 majorVersion, u32 minorVersion);
  XcbCookie              (SYS_DECL* select_input)(XcbConnection*, XcbWindow, u16 enable);
  XRandrCrtcInfo*        (SYS_DECL* get_crtc_info_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  XRandrModeInfoIterator (SYS_DECL* get_screen_resources_current_modes_iterator)(const XRandrScreenResources*);
  XRandrOutput*          (SYS_DECL* get_screen_resources_current_outputs)(const XRandrScreenResources*);
  XRandrOutputInfo*      (SYS_DECL* get_output_info_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  XRandrScreenResources* (SYS_DECL* get_screen_resources_current_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  // clang-format on
} XRandr;

typedef struct {
  DynLib*       lib;
  XcbExtension* id;
  XcbPictFormat formatArgb32;

  // clang-format off
  void                 (SYS_DECL* pictforminfo_next)(XcbPictFormatInfoItr*);
  void*                (SYS_DECL* query_version_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  XcbCookie            (SYS_DECL* create_cursor)(XcbConnection*, XcbCursor, XcbPicture, u16 x, u16 y);
  XcbCookie            (SYS_DECL* create_picture)(XcbConnection*, XcbPicture, XcbDrawable, XcbPictFormat, u32 valueMask, const void* valueList);
  XcbCookie            (SYS_DECL* free_picture)(XcbConnection*, XcbPicture);
  XcbCookie            (SYS_DECL* query_pict_formats)(XcbConnection*);
  XcbCookie            (SYS_DECL* query_version)(XcbConnection*, u32 majorVersion, u32 minorVersion);
  XcbPictFormatInfoItr (SYS_DECL* query_pict_formats_formats_iterator)(const XcbPictFormats*);
  XcbPictFormats*      (SYS_DECL* query_pict_formats_reply)(XcbConnection*, XcbCookie, XcbGenericError**);
  // clang-format on
} XRender;

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

  GapPalXcbExtFlags extensions;
  GapPalFlags       flags;

  Xcb       xcb;
  XkbCommon xkb;
  XFixes    xfixes;
  XRandr    xrandr;
  XRender   xrender;

  Mem       icons[GapIcon_Count];
  XcbCursor cursors[GapCursor_Count];
};

// clang-format off
static const XcbEventMask g_xcbWindowEventMask =
  1       /* XCB_EVENT_MASK_KEY_PRESS */        |
  2       /* XCB_EVENT_MASK_KEY_RELEASE */      |
  4       /* XCB_EVENT_MASK_BUTTON_PRESS */     |
  8       /* XCB_EVENT_MASK_BUTTON_RELEASE */   |
  64      /* XCB_EVENT_MASK_POINTER_MOTION */   |
  131072  /* XCB_EVENT_MASK_STRUCTURE_NOTIFY */ |
  2097152 /* XCB_EVENT_MASK_FOCUS_CHANGE */     |
  4194304 /* XCB_EVENT_MASK_PROPERTY_CHANGE */;
// clang-format on

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
  case 1 /* XCB_CONN_ERROR */:
    return string_lit("Connection error");
  case 2 /* XCB_CONN_CLOSED_EXT_NOTSUPPORTED */:
    return string_lit("Extension not supported");
  case 3 /* XCB_CONN_CLOSED_MEM_INSUFFICIENT */:
    return string_lit("Insufficient memory available");
  case 4 /* XCB_CONN_CLOSED_REQ_LEN_EXCEED */:
    return string_lit("Request length exceeded");
  case 5 /* XCB_CONN_CLOSED_PARSE_ERR */:
    return string_lit("Failed to parse display string");
  case 6 /* XCB_CONN_CLOSED_INVALID_SCREEN */:
    return string_lit("No valid screen available");
  default:
    return string_lit("Unknown error");
  }
}

static GapKey pal_xcb_translate_key(const XkbKeycode key) {
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
static XcbAtom pal_xcb_atom(GapPal* pal, const String name) {
  XcbGenericError* err = null;
  XcbAtomData* data    = xcb_call(pal->xcb.con, pal->xcb.intern_atom, &err, 0, name.size, name.ptr);
  if (UNLIKELY(err)) {
    diag_crash_msg(
        "Failed to retrieve Xcb atom: {}, err: {}", fmt_text(name), fmt_int(err->errorCode));
  }
  const XcbAtom result = data->atom;
  free(data);
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

  XCB_LOAD_SYM(change_property);
  XCB_LOAD_SYM(change_window_attributes);
  XCB_LOAD_SYM(configure_window);
  XCB_LOAD_SYM(connect);
  XCB_LOAD_SYM(connection_has_error);
  XCB_LOAD_SYM(request_check);
  XCB_LOAD_SYM(convert_selection);
  XCB_LOAD_SYM(create_gc);
  XCB_LOAD_SYM(create_pixmap);
  XCB_LOAD_SYM(create_window);
  XCB_LOAD_SYM(delete_property);
  XCB_LOAD_SYM(delete_property);
  XCB_LOAD_SYM(destroy_window);
  XCB_LOAD_SYM(disconnect);
  XCB_LOAD_SYM(flush);
  XCB_LOAD_SYM(free_cursor);
  XCB_LOAD_SYM(free_gc);
  XCB_LOAD_SYM(free_pixmap);
  XCB_LOAD_SYM(generate_id);
  XCB_LOAD_SYM(get_extension_data);
  XCB_LOAD_SYM(get_file_descriptor);
  XCB_LOAD_SYM(get_maximum_request_length);
  XCB_LOAD_SYM(get_property_reply);
  XCB_LOAD_SYM(get_property_value);
  XCB_LOAD_SYM(get_property);
  XCB_LOAD_SYM(get_setup);
  XCB_LOAD_SYM(grab_pointer);
  XCB_LOAD_SYM(intern_atom_reply);
  XCB_LOAD_SYM(intern_atom);
  XCB_LOAD_SYM(map_window);
  XCB_LOAD_SYM(poll_for_event);
  XCB_LOAD_SYM(put_image);
  XCB_LOAD_SYM(query_pointer_reply);
  XCB_LOAD_SYM(query_pointer);
  XCB_LOAD_SYM(send_event);
  XCB_LOAD_SYM(set_selection_owner);
  XCB_LOAD_SYM(setup_roots_iterator);
  XCB_LOAD_SYM(ungrab_pointer);
  XCB_LOAD_SYM(warp_pointer);

#undef XCB_LOAD_SYM

  // Establish a connection with the x-server.
  int screen         = 0;
  out->con           = out->connect(null, &screen);
  out->maxRequestLen = out->get_maximum_request_length(out->con) * 4;

  // Find the screen for our connection.
  const XcbSetup* setup     = out->get_setup(out->con);
  XcbScreenItr    screenItr = out->setup_roots_iterator(setup);
  if (!screenItr.data) {
    diag_crash_msg("Xcb no screen found");
  }
  out->screen = screenItr.data;

  // Retrieve atoms to use while communicating with the x-server.
  out->atomProtoMsg                = pal_xcb_atom(pal, string_lit("WM_PROTOCOLS"));
  out->atomDeleteMsg               = pal_xcb_atom(pal, string_lit("WM_DELETE_WINDOW"));
  out->atomWmIcon                  = pal_xcb_atom(pal, string_lit("_NET_WM_ICON"));
  out->atomWmState                 = pal_xcb_atom(pal, string_lit("_NET_WM_STATE"));
  out->atomWmStateFullscreen       = pal_xcb_atom(pal, string_lit("_NET_WM_STATE_FULLSCREEN"));
  out->atomWmStateBypassCompositor = pal_xcb_atom(pal, string_lit("_NET_WM_BYPASS_COMPOSITOR"));
  out->atomClipboard               = pal_xcb_atom(pal, string_lit("CLIPBOARD"));
  out->atomVoloClipboard           = pal_xcb_atom(pal, string_lit("VOLO_CLIPBOARD"));
  out->atomTargets                 = pal_xcb_atom(pal, string_lit("TARGETS"));
  out->atomUtf8String              = pal_xcb_atom(pal, string_lit("UTF8_STRING"));
  out->atomPlainUtf8               = pal_xcb_atom(pal, string_lit("text/plain;charset=utf-8"));

  MAYBE_UNUSED const GapVector screenSize =
      gap_vector(out->screen->widthInPixels, out->screen->heightInPixels);

  log_i(
      "Xcb connected",
      log_param("fd", fmt_int(out->get_file_descriptor(out->con))),
      log_param("max-req-length", fmt_size(out->maxRequestLen)),
      log_param("screen-num", fmt_int(screen)),
      log_param("screen-size", gap_vector_fmt(screenSize)));
}

static void pal_xcb_wm_state_update(
    GapPal* pal, const GapWindowId windowId, const XcbAtom stateAtom, const bool active) {
  const XcbClientMessageEvent evt = {
      .responseType = 33 /* XCB_CLIENT_MESSAGE */,
      .format       = sizeof(XcbAtom) * 8,
      .window       = (XcbWindow)windowId,
      .type         = pal->xcb.atomWmState,
      .data[0]      = active,
      .data[1]      = stateAtom,
  };
  const u32 mask = 131072 /* XCB_EVENT_MASK_STRUCTURE_NOTIFY */ |
                   1048576 /* XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT */;
  pal->xcb.send_event(pal->xcb.con, false, pal->xcb.screen->root, mask, (const char*)&evt);
}

static void pal_xcb_bypass_compositor(GapPal* pal, const GapWindowId windowId, const bool active) {
  const u32 value = active;
  pal->xcb.change_property(
      pal->xcb.con,
      0 /* XCB_PROP_MODE_REPLACE */,
      (XcbWindow)windowId,
      pal->xcb.atomWmStateBypassCompositor,
      6 /* XCB_ATOM_CARDINAL */,
      sizeof(u32) * 8,
      1,
      (const char*)&value);
}

static void pal_xcb_cursor_grab(GapPal* pal, const GapWindowId windowId) {
  const u16 mask = 4 /* XCB_EVENT_MASK_BUTTON_PRESS */ | 8 /* XCB_EVENT_MASK_BUTTON_RELEASE */ |
                   64 /* XCB_EVENT_MASK_POINTER_MOTION */;
  pal->xcb.grab_pointer(
      pal->xcb.con,
      true,
      (XcbWindow)windowId,
      mask,
      1 /* XCB_GRAB_MODE_ASYNC */,
      1 /* XCB_GRAB_MODE_ASYNC */,
      (XcbWindow)windowId,
      0,
      0);
}

static void pal_xcb_cursor_grab_release(GapPal* pal) { pal->xcb.ungrab_pointer(pal->xcb.con, 0); }

static void pal_xkb_enable_flag(GapPal* pal, const i32 flag) {
  const u16 deviceSpec = 256 /* XCB_XKB_ID_USE_CORE_KBD */;
  pal->xkb.per_client_flags_unchecked(pal->xcb.con, deviceSpec, flag, flag, 0, 0, 0);
}

/**
 * Initialize the xkb extension, gives us additional control over keyboard input.
 * More info: https://en.wikipedia.org/wiki/X_keyboard_extension
 */
static bool pal_init_xkb(GapPal* pal, XkbCommon* out) {
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

  XKB_LOAD_SYM(xcb_xkb, select_events_checked);
  XKB_LOAD_SYM(xcb_xkb, per_client_flags_unchecked);
  XKB_LOAD_SYM(xkb_x11, get_core_keyboard_device_id);
  XKB_LOAD_SYM(xkb_x11, keymap_new_from_device);
  XKB_LOAD_SYM(xkb_x11, setup_xkb_extension);
  XKB_LOAD_SYM(xkb_x11, state_new_from_device);
  XKB_LOAD_SYM(xkb, context_new);
  XKB_LOAD_SYM(xkb, context_unref);
  XKB_LOAD_SYM(xkb, keymap_layout_get_name);
  XKB_LOAD_SYM(xkb, keymap_num_layouts);
  XKB_LOAD_SYM(xkb, keymap_unref);
  XKB_LOAD_SYM(xkb, state_key_get_utf8);
  XKB_LOAD_SYM(xkb, state_unref);
  XKB_LOAD_SYM(xkb, state_update_mask);

#undef XKB_LOAD_SYM

  XcbGenericError* err = null;

  u16       versionMajor;
  u16       versionMinor;
  const int setupRes = out->setup_xkb_extension(
      pal->xcb.con, 1, 0, 0, &versionMajor, &versionMinor, &out->firstEvent, &out->firstError);

  if (UNLIKELY(!setupRes)) {
    log_w("Failed to initialize Xkb", log_param("error", fmt_int(err->errorCode)));
    free(err);
    return false;
  }

  const u16       deviceSpec = 256 /* XCB_XKB_ID_USE_CORE_KBD */;
  const u16       eventTypes = 4 /* XCB_XKB_EVENT_TYPE_STATE_NOTIFY */;
  const XcbCookie selectEventsCookie =
      out->select_events_checked(pal->xcb.con, deviceSpec, eventTypes, 0, eventTypes, 0, 0, null);
  err = pal->xcb.request_check(pal->xcb.con, selectEventsCookie);
  if (UNLIKELY(err)) {
    log_w("Failed to select Xkb events", log_param("error", fmt_int(err->errorCode)));
    free(err);
    return false;
  }

  out->context = out->context_new(0);
  if (UNLIKELY(!out->context)) {
    log_w("Failed to create the XkbCommon context");
    return false;
  }
  out->deviceId = out->get_core_keyboard_device_id(pal->xcb.con);
  if (UNLIKELY(out->deviceId < 0)) {
    log_w("Failed to retrieve the Xkb keyboard device-id");
    return false;
  }
  out->keymap = out->keymap_new_from_device(out->context, pal->xcb.con, out->deviceId, 0);
  if (!out->keymap) {
    log_w("Failed to retrieve the Xkb keyboard keymap");
    return false;
  }
  out->state = out->state_new_from_device(out->keymap, pal->xcb.con, out->deviceId);
  if (!out->keymap) {
    log_w("Failed to retrieve the Xkb keyboard state");
    return false;
  }

  const u32    layoutCount   = out->keymap_num_layouts(out->keymap);
  const char*  layoutNameRaw = out->keymap_layout_get_name(out->keymap, 0);
  const String layoutName    = layoutNameRaw ? string_from_null_term(layoutNameRaw) : string_empty;

  log_i(
      "Initialized XkbCommon",
      log_param("path", fmt_path(dynlib_path(out->lib))),
      log_param("version", fmt_list_lit(fmt_int(versionMajor), fmt_int(versionMinor))),
      log_param("device-id", fmt_int(out->deviceId)),
      log_param("layout-count", fmt_int(layoutCount)),
      log_param("main-layout-name", fmt_text(layoutName)));
  return true;
}

/**
 * Initialize xfixes extension, contains various utilities.
 */
static bool pal_init_xfixes(GapPal* pal, XFixes* out) {
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

  XFIXES_LOAD_SYM(hide_cursor);
  XFIXES_LOAD_SYM(query_version_reply);
  XFIXES_LOAD_SYM(query_version);
  XFIXES_LOAD_SYM(show_cursor);

#undef XFIXES_LOAD_SYM

  XcbGenericError* err   = null;
  void*            reply = xcb_call(pal->xcb.con, out->query_version, &err, 5, 0);
  free(reply);

  if (UNLIKELY(err)) {
    log_w("Failed to initialize XFixes", log_param("error", fmt_int(err->errorCode)));
    return false;
  }

  log_i("Initialized XFixes", log_param("path", fmt_path(dynlib_path(out->lib))));
  return true;
}

/**
 * Initialize the RandR extension.
 * More info: https://xcb.freedesktop.org/manual/group__XCB__RandR__API.html
 */
static bool pal_init_randr(GapPal* pal, XRandr* out) {
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

  XRANDR_LOAD_SYM(get_crtc_info_reply);
  XRANDR_LOAD_SYM(get_crtc_info);
  XRANDR_LOAD_SYM(get_output_info_name_length);
  XRANDR_LOAD_SYM(get_output_info_name);
  XRANDR_LOAD_SYM(get_output_info_reply);
  XRANDR_LOAD_SYM(get_output_info);
  XRANDR_LOAD_SYM(get_screen_resources_current_modes_iterator);
  XRANDR_LOAD_SYM(get_screen_resources_current_outputs_length);
  XRANDR_LOAD_SYM(get_screen_resources_current_outputs);
  XRANDR_LOAD_SYM(get_screen_resources_current_reply);
  XRANDR_LOAD_SYM(get_screen_resources_current);
  XRANDR_LOAD_SYM(id);
  XRANDR_LOAD_SYM(mode_info_next);
  XRANDR_LOAD_SYM(query_version_reply);
  XRANDR_LOAD_SYM(query_version);
  XRANDR_LOAD_SYM(select_input);

#undef XRANDR_LOAD_SYM

  const XcbExtensionData* data = pal->xcb.get_extension_data(pal->xcb.con, out->id);
  if (!data || !data->present) {
    log_w("XRandR extention not present");
    return false;
  }
  XcbGenericError* err     = null;
  void*            version = xcb_call(pal->xcb.con, out->query_version, &err, 1, 6);
  free(version);

  if (UNLIKELY(err)) {
    log_w("Failed to initialize XRandR", log_param("err", fmt_int(err->errorCode)));
    return false;
  }

  out->firstEvent = data->first_event;
  log_i("Initialized XRandR", log_param("path", fmt_path(dynlib_path(out->lib))));
  return true;
}

static bool pal_xrender_find_formats(GapPal* pal) {
  XcbGenericError* err     = null;
  XcbPictFormats*  formats = xcb_call_void(pal->xcb.con, pal->xrender.query_pict_formats, &err);

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
    pal->xrender.formatArgb32 = itr.data->id;
    return true;
  }

  free(formats);
  return false; // Argb32 not found.
}

static bool pal_init_xrender(GapPal* pal, XRender* out) {
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
      log_w("XRender symbol '{}' missing", log_param("sym", fmt_text(symName)));                   \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  XRENDER_LOAD_SYM(create_cursor);
  XRENDER_LOAD_SYM(create_picture);
  XRENDER_LOAD_SYM(free_picture);
  XRENDER_LOAD_SYM(id);
  XRENDER_LOAD_SYM(pictforminfo_next);
  XRENDER_LOAD_SYM(query_pict_formats_formats_iterator);
  XRENDER_LOAD_SYM(query_pict_formats_reply);
  XRENDER_LOAD_SYM(query_pict_formats);
  XRENDER_LOAD_SYM(query_version_reply);
  XRENDER_LOAD_SYM(query_version);

#undef XRENDER_LOAD_SYM

  const XcbExtensionData* data = pal->xcb.get_extension_data(pal->xcb.con, out->id);
  if (!data || !data->present) {
    log_w("XRender extention not present");
    return false;
  }
  XcbGenericError* err     = null;
  void*            version = xcb_call(pal->xcb.con, out->query_version, &err, 0, 11);
  free(version);

  if (UNLIKELY(err)) {
    log_w("Failed to initialize XRender", log_param("err", fmt_int(err->errorCode)));
    return false;
  }
  if (!pal_xrender_find_formats(pal)) {
    log_w("XRenderfailed to find required render formats");
    return false;
  }

  log_i("Initialized XRender", log_param("path", fmt_path(dynlib_path(out->lib))));
  return true;
}

static void pal_init_extensions(GapPal* pal) {
  if (pal_init_xkb(pal, &pal->xkb)) {
    pal->extensions |= GapPalXcbExtFlags_Xkb;
  }
  if (pal_init_xfixes(pal, &pal->xfixes)) {
    pal->extensions |= GapPalXcbExtFlags_XFixes;
  }
  if (pal_init_randr(pal, &pal->xrandr)) {
    pal->extensions |= GapPalXcbExtFlags_Randr;
  }
  if (pal_init_xrender(pal, &pal->xrender)) {
    pal->extensions |= GapPalXcbExtFlags_Render;
  }
}

static f32 pal_randr_refresh_rate(GapPal* pal, XRandrScreenResources* s, const XRandrMode mode) {
  XRandrModeInfoIterator i = pal->xrandr.get_screen_resources_current_modes_iterator(s);
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

  XcbGenericError*       err = null;
  XRandrScreenResources* screen =
      xcb_call(pal->xcb.con, pal->xrandr.get_screen_resources_current, &err, pal->xcb.screen->root);
  if (UNLIKELY(err)) {
    diag_crash_msg("Failed to retrieve XRandR screen-info, err: {}", fmt_int(err->errorCode));
  }

  const XRandrOutput* outputs    = pal->xrandr.get_screen_resources_current_outputs(screen);
  const u32           numOutputs = pal->xrandr.get_screen_resources_current_outputs_length(screen);
  for (u32 i = 0; i != numOutputs; ++i) {
    XRandrOutputInfo* output =
        xcb_call(pal->xcb.con, pal->xrandr.get_output_info, &err, outputs[i], 0);
    if (UNLIKELY(err)) {
      diag_crash_msg("Failed to retrieve XRandR output-info, err: {}", fmt_int(err->errorCode));
    }
    const String name = {
        .ptr  = pal->xrandr.get_output_info_name(output),
        .size = pal->xrandr.get_output_info_name_length(output),
    };

    if (output->crtc) {
      XRandrCrtcInfo* crtc =
          xcb_call(pal->xcb.con, pal->xrandr.get_crtc_info, &err, output->crtc, 0);
      if (UNLIKELY(err)) {
        diag_crash_msg("Failed to retrieve XRandR crtc-info, err: {}", fmt_int(err->errorCode));
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

static GapVector pal_query_cursor_pos(GapPal* pal, const GapWindowId winId) {
  GapPalWindow* window = pal_maybe_window(pal, winId);
  if (!window) {
    return gap_vector(0, 0);
  }

  GapVector        result = gap_vector(0, 0);
  XcbGenericError* err    = null;
  XcbPointerData*  data   = xcb_call(pal->xcb.con, pal->xcb.query_pointer, &err, (XcbWindow)winId);

  if (UNLIKELY(err)) {
    log_w(
        "Failed to query the x11 cursor position",
        log_param("window-id", fmt_int(winId)),
        log_param("error", fmt_int(err->errorCode)));
    goto Return;
  }

  // Xcb uses top-left as opposed to bottom-left, so we have to remap the y coordinate.
  result = (GapVector){
      .x = data->winX,
      .y = window->params[GapParam_WindowSize].height - data->winY,
  };

Return:
  free(data);
  return result;
}

static void pal_set_window_min_size(GapPal* pal, const GapWindowId windowId, const GapVector val) {
  // Needs to match 'WinXSizeHints' from the XServer.
  struct SizeHints {
    u32 flags;
    i32 x, y;
    i32 width, height;
    i32 minWidth, minHeight;
    i32 maxWidth, maxHeight;
    i32 widthInc, heightInc;
    i32 minAspectNum, minAspectDen;
    i32 maxAspectNum, maxAspectDen;
    i32 baseWidth, baseHeight;
    u32 winGravity;
  };

  const struct SizeHints newHints = {
      .flags     = 1 << 4 /* PMinSize */,
      .minWidth  = val.width,
      .minHeight = val.height,
  };

  pal->xcb.change_property(
      pal->xcb.con,
      0 /* XCB_PROP_MODE_REPLACE */,
      (XcbWindow)windowId,
      40 /* XCB_ATOM_WM_NORMAL_HINTS */,
      41 /* XCB_ATOM_WM_SIZE_HINTS */,
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

static void pal_event_text(GapPal* pal, const GapWindowId windowId, const XkbKeycode keyCode) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (UNLIKELY(!window)) {
    return;
  }
  if (pal->extensions & GapPalXcbExtFlags_Xkb) {
    char      buff[32];
    const int textSize = pal->xkb.state_key_get_utf8(pal->xkb.state, keyCode, buff, sizeof(buff));
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

static void pal_clip_send_targets(GapPal* pal, const XcbWindow requestor, const XcbAtom property) {
  const XcbAtom targets[] = {
      pal->xcb.atomTargets,
      pal->xcb.atomUtf8String,
      pal->xcb.atomPlainUtf8,
  };
  pal->xcb.change_property(
      pal->xcb.con,
      0 /* XCB_PROP_MODE_REPLACE */,
      requestor,
      property,
      4 /* XCB_ATOM_ATOM */,
      sizeof(XcbAtom) * 8,
      array_elems(targets),
      (const char*)targets);
}

static void pal_clip_send_utf8(
    GapPal* pal, const GapPalWindow* window, const XcbWindow requestor, const XcbAtom property) {
  pal->xcb.change_property(
      pal->xcb.con,
      0 /* XCB_PROP_MODE_REPLACE */,
      requestor,
      property,
      pal->xcb.atomUtf8String,
      sizeof(u8) * 8,
      (u32)window->clipCopy.size,
      window->clipCopy.ptr);
}

static void pal_event_clip_copy_request(
    GapPal* pal, const GapWindowId windowId, const XcbSelectionRequestEvent* req) {

  XcbSelectionNotifyEvent notifyEvt = {
      .responseType = 31 /* XCB_SELECTION_NOTIFY */,
      .time         = 0,
      .requestor    = req->requestor,
      .selection    = req->selection,
      .target       = req->target,
  };

  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (window && req->selection == pal->xcb.atomClipboard && !string_is_empty(window->clipCopy)) {
    /**
     * Either return a collection of targets (think format types) of the clipboard data, or the data
     * itself as utf8.
     */
    if (req->target == pal->xcb.atomTargets) {
      pal_clip_send_targets(pal, req->requestor, req->property);
      notifyEvt.property = req->property;
    } else if (req->target == pal->xcb.atomUtf8String || req->target == pal->xcb.atomPlainUtf8) {
      pal_clip_send_utf8(pal, window, req->requestor, req->property);
      notifyEvt.property = req->property;
    } else {
      log_w("Xcb copy request for unsupported target received");
    }
  }

  const u32 mask = 4194304 /* XCB_EVENT_MASK_PROPERTY_CHANGE */;
  pal->xcb.send_event(pal->xcb.con, 0, req->requestor, mask, (char*)&notifyEvt);
}

static void pal_event_clip_paste_notify(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  if (!window) {
    return;
  }
  XcbGenericError* err   = null;
  XcbPropertyData* reply = xcb_call(
      pal->xcb.con,
      pal->xcb.get_property,
      &err,
      0,
      (XcbWindow)windowId,
      pal->xcb.atomVoloClipboard,
      0,
      0,
      (u32)(pal->xcb.maxRequestLen / 4));
  if (UNLIKELY(err)) {
    diag_crash_msg("Xcb failed to retrieve clipboard value, err: {}", fmt_int(err->errorCode));
  }

  string_maybe_free(g_allocHeap, window->clipPaste);
  if (reply->valueLen) {
    const String selectionMem = mem_create(pal->xcb.get_property_value(reply), reply->valueLen);
    window->clipPaste         = string_dup(g_allocHeap, selectionMem);
    window->flags |= GapPalWindowFlags_ClipPaste;
  } else {
    window->clipPaste = string_empty;
  }
  free(reply);

  pal->xcb.delete_property(pal->xcb.con, (XcbWindow)windowId, pal->xcb.atomVoloClipboard);
}

GapPal* gap_pal_create(Allocator* alloc) {
  GapPal* pal = alloc_alloc_t(alloc, GapPal);

  *pal = (GapPal){
      .alloc    = alloc,
      .windows  = dynarray_create_t(alloc, GapPalWindow, 1),
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

  if (pal->xkb.context) {
    pal->xkb.context_unref(pal->xkb.context);
  }
  if (pal->xkb.keymap) {
    pal->xkb.keymap_unref(pal->xkb.keymap);
  }
  if (pal->xkb.state) {
    pal->xkb.state_unref(pal->xkb.state);
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
  array_for_t(pal->cursors, XcbCursor, cursor) {
    if (*cursor) {
      pal->xcb.free_cursor(pal->xcb.con, *cursor);
    }
  }

  pal->xcb.disconnect(pal->xcb.con);
  log_i("Xcb disconnected");

  dynarray_destroy(&pal->windows);
  dynarray_destroy(&pal->displays);
  alloc_free_t(pal->alloc, pal);
}

void gap_pal_update(GapPal* pal) {
  // Clear volatile state, like the key-presses from the previous update.
  pal_clear_volatile(pal);

  // Handle all xcb events in the buffer.
  for (XcbGenericEvent* evt; (evt = pal->xcb.poll_for_event(pal->xcb.con)); free(evt)) {
    const u32 eventId = evt->responseType & ~0x80;
    switch (eventId) {

    case 0: {
      const XcbGenericError* errMsg = (const void*)evt;
      log_e(
          "Xcb error",
          log_param("code", fmt_int(errMsg->errorCode)),
          log_param("msg", fmt_text(pal_xcb_err_str(errMsg->errorCode))));
    } break;

    case 33 /* XCB_CLIENT_MESSAGE */: {
      const XcbClientMessageEvent* clientMsg = (const void*)evt;
      if (clientMsg->data[0] == pal->xcb.atomDeleteMsg) {
        pal_event_close(pal, clientMsg->window);
      }
    } break;

    case 9 /* XCB_FOCUS_IN */: {
      const XcbFocusEvent* focusInMsg = (const void*)evt;
      pal_event_focus_gained(pal, focusInMsg->event);

      if (pal_maybe_window(pal, focusInMsg->event)) {
        // Update the cursor as it was probably moved since we where focussed last.
        pal_event_cursor(pal, focusInMsg->event, pal_query_cursor_pos(pal, focusInMsg->event));
      }
    } break;

    case 10 /* XCB_FOCUS_OUT */: {
      const XcbFocusEvent* focusOutMsg = (const void*)evt;
      pal_event_focus_lost(pal, focusOutMsg->event);
    } break;

    case 22 /* XCB_CONFIGURE_NOTIFY */: {
      const XcbConfigureNotifyEvent* configureMsg = (const void*)evt;
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

    case 6 /* XCB_MOTION_NOTIFY */: {
      const XcbMotionNotifyEvent* motionMsg = (const void*)evt;
      GapPalWindow*               window    = pal_maybe_window(pal, motionMsg->event);
      if (window) {
        // Xcb uses top-left as opposed to bottom-left, so we have to remap the y coordinate.
        const GapVector newPos = {
            motionMsg->eventX,
            window->params[GapParam_WindowSize].height - motionMsg->eventY,
        };
        pal_event_cursor(pal, motionMsg->event, newPos);
      }
    } break;

    case 4 /* XCB_BUTTON_PRESS */: {
      const XcbButtonEvent* pressMsg = (const void*)evt;
      switch (pressMsg->detail) {
      case 1 /* XCB_BUTTON_INDEX_1 */:
        pal_event_press(pal, pressMsg->event, GapKey_MouseLeft);
        break;
      case 2 /* XCB_BUTTON_INDEX_2 */:
        pal_event_press(pal, pressMsg->event, GapKey_MouseMiddle);
        break;
      case 3 /* XCB_BUTTON_INDEX_3 */:
        pal_event_press(pal, pressMsg->event, GapKey_MouseRight);
        break;
      case 4 /* XCB_BUTTON_INDEX_4 */: // Mouse-wheel scroll up.
        pal_event_scroll(pal, pressMsg->event, gap_vector(0, 1));
        break;
      case 5 /* XCB_BUTTON_INDEX_5 */: // Mouse-wheel scroll down.
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

    case 5 /* XCB_BUTTON_RELEASE */: {
      const XcbButtonEvent* releaseMsg = (const void*)evt;
      switch (releaseMsg->detail) {
      case 1 /* XCB_BUTTON_INDEX_1 */:
        pal_event_release(pal, releaseMsg->event, GapKey_MouseLeft);
        break;
      case 2 /* XCB_BUTTON_INDEX_2 */:
        pal_event_release(pal, releaseMsg->event, GapKey_MouseMiddle);
        break;
      case 3 /* XCB_BUTTON_INDEX_3 */:
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

    case 2 /* XCB_KEY_PRESS */: {
      const XcbKeyEvent* pressMsg = (const void*)evt;
      pal_event_press(pal, pressMsg->event, pal_xcb_translate_key(pressMsg->detail));
      pal_event_text(pal, pressMsg->event, pressMsg->detail);
    } break;

    case 3 /* XCB_KEY_RELEASE */: {
      const XcbKeyEvent* releaseMsg = (const void*)evt;
      pal_event_release(pal, releaseMsg->event, pal_xcb_translate_key(releaseMsg->detail));
    } break;

    case 29 /* XCB_SELECTION_CLEAR */: {
      const XcbSelectionClearEvent* selectionClearMsg = (const void*)evt;
      pal_event_clip_copy_clear(pal, selectionClearMsg->owner);
    } break;

    case 30 /* XCB_SELECTION_REQUEST */: {
      const XcbSelectionRequestEvent* selectionRequestMsg = (const void*)evt;
      pal_event_clip_copy_request(pal, selectionRequestMsg->owner, selectionRequestMsg);
    } break;

    case 31 /* XCB_SELECTION_NOTIFY */: {
      const XcbSelectionNotifyEvent* selectionNotifyMsg = (const void*)evt;
      if (selectionNotifyMsg->selection == pal->xcb.atomClipboard && selectionNotifyMsg->target) {
        pal_event_clip_paste_notify(pal, selectionNotifyMsg->requestor);
      }
    } break;
    default:
      if (pal->extensions & GapPalXcbExtFlags_Randr && eventId >= pal->xrandr.firstEvent) {
        switch (eventId - pal->xrandr.firstEvent) {
        case 0 /* XCB_RANDR_SCREEN_CHANGE_NOTIFY */: {
          const XRandrScreenChangeEvent* screenChangeMsg = (const void*)evt;

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
      if (pal->extensions & GapPalXcbExtFlags_Xkb && eventId == pal->xkb.firstEvent) {
        const XkbEventAny* xkbEvent = (const void*)evt;
        if (xkbEvent->deviceId == pal->xkb.deviceId) {
          switch (xkbEvent->xkbType) {
          case 2 /* XCB_XKB_STATE_NOTIFY */: {
            const XkbEventStateNotify* stateNotify = (const void*)evt;
            pal->xkb.state_update_mask(
                pal->xkb.state,
                stateNotify->baseMods,
                stateNotify->latchedMods,
                stateNotify->lockedMods,
                stateNotify->baseGroup,
                stateNotify->latchedGroup,
                stateNotify->lockedGroup);
          } break;
          }
        }
      }
    }
  }
}

void gap_pal_flush(GapPal* pal) {
  pal->xcb.flush(pal->xcb.con);

  const int error = pal->xcb.connection_has_error(pal->xcb.con);
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

  XcbPixmap pixmap = pal->xcb.generate_id(pal->xcb.con);
  pal->xcb.create_pixmap(
      pal->xcb.con, 32, pixmap, pal->xcb.screen->root, asset->width, asset->height);

  XcbPicture picture = pal->xcb.generate_id(pal->xcb.con);
  pal->xrender.create_picture(pal->xcb.con, picture, pixmap, pal->xrender.formatArgb32, 0, null);

  XcbGcContext graphicsContext = pal->xcb.generate_id(pal->xcb.con);
  pal->xcb.create_gc(pal->xcb.con, graphicsContext, pixmap, 0, null);

  Mem pixelBuffer = alloc_alloc(g_allocScratch, asset->width * asset->height * 4, 4);
  gap_pal_icon_to_argb_flipped(asset, pixelBuffer);

  pal->xcb.put_image(
      pal->xcb.con,
      2 /* XCB_IMAGE_FORMAT_Z_PIXMAP */,
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

  pal->xcb.free_gc(pal->xcb.con, graphicsContext);

  XcbCursor cursor = pal->xcb.generate_id(pal->xcb.con);
  pal->xrender.create_cursor(
      pal->xcb.con, cursor, picture, asset->hotspotX, asset->height - asset->hotspotY);

  pal->xrender.free_picture(pal->xcb.con, picture);
  pal->xcb.free_pixmap(pal->xcb.con, pixmap);

  if (pal->cursors[id]) {
    pal->xcb.free_cursor(pal->xcb.con, pal->cursors[id]);
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
  XcbConnection*    con = pal->xcb.con;
  const GapWindowId id  = pal->xcb.generate_id(con);

  if (size.width <= 0) {
    size.width = pal->xcb.screen->widthInPixels;
  } else if (size.width < pal_window_min_width) {
    size.width = pal_window_min_width;
  }
  if (size.height <= 0) {
    size.height = pal->xcb.screen->heightInPixels;
  } else if (size.height < pal_window_min_height) {
    size.height = pal_window_min_height;
  }

  const u32 valuesMask = 2 /* XCB_CW_BACK_PIXEL */ | 2048 /* XCB_CW_EVENT_MASK */;
  const u32 values[2]  = {
      pal->xcb.screen->blackPixel,
      g_xcbWindowEventMask,
  };

  pal->xcb.create_window(
      con,
      0 /* XCB_COPY_FROM_PARENT */,
      (XcbWindow)id,
      pal->xcb.screen->root,
      0,
      0,
      (u16)size.width,
      (u16)size.height,
      0,
      1 /* XCB_WINDOW_CLASS_INPUT_OUTPUT */,
      pal->xcb.screen->rootVisual,
      valuesMask,
      values);

  // Register a custom delete message atom.
  pal->xcb.change_property(
      con,
      0 /* XCB_PROP_MODE_REPLACE */,
      (XcbWindow)id,
      pal->xcb.atomProtoMsg,
      4 /* XCB_ATOM_ATOM */,
      sizeof(XcbAtom) * 8,
      1,
      &pal->xcb.atomDeleteMsg);

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
    pal->xrandr.select_input(pal->xcb.con, (XcbWindow)id, mask);
  }

  gap_pal_window_icon_set(pal, id, GapIcon_Main);
  pal_set_window_min_size(pal, id, gap_vector(pal_window_min_width, pal_window_min_height));
  pal->xcb.map_window(con, (XcbWindow)id);

  log_i("Window created", log_param("id", fmt_int(id)), log_param("size", gap_vector_fmt(size)));

  return id;
}

void gap_pal_window_destroy(GapPal* pal, const GapWindowId windowId) {

  pal->xcb.destroy_window(pal->xcb.con, (XcbWindow)windowId);

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
  pal->xcb.change_property(
      pal->xcb.con,
      0 /* XCB_PROP_MODE_REPLACE */,
      (XcbWindow)windowId,
      39 /* XCB_ATOM_WM_NAME */,
      pal->xcb.atomUtf8String,
      sizeof(u8) * 8,
      (u32)title.size,
      title.ptr);
}

void gap_pal_window_resize(
    GapPal* pal, const GapWindowId windowId, GapVector size, const bool fullscreen) {

  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  if (size.width <= 0) {
    size.width = pal->xcb.screen->widthInPixels;
  } else if (size.width < pal_window_min_width) {
    size.width = pal_window_min_width;
  }

  if (size.height <= 0) {
    size.height = pal->xcb.screen->heightInPixels;
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
    pal_xcb_wm_state_update(pal, windowId, pal->xcb.atomWmStateFullscreen, true);
    pal_xcb_bypass_compositor(pal, windowId, true);
  } else {
    window->flags &= ~GapPalWindowFlags_Fullscreen;

    pal_xcb_wm_state_update(pal, windowId, pal->xcb.atomWmStateFullscreen, false);
    pal_xcb_bypass_compositor(pal, windowId, false);

    const u16 valueMask = 4 /* XCB_CONFIG_WINDOW_WIDTH */ | 8 /* XCB_CONFIG_WINDOW_HEIGHT */;
    const u32 values[2] = {size.x, size.y};
    pal->xcb.configure_window(pal->xcb.con, (XcbWindow)windowId, valueMask, values);
  }
}

void gap_pal_window_cursor_hide(GapPal* pal, const GapWindowId windowId, const bool hidden) {
  if (!(pal->extensions & GapPalXcbExtFlags_XFixes)) {
    log_w("Failed to update cursor visibility: XFixes extension not available");
    return;
  }

  if (hidden && !(pal->flags & GapPalFlags_CursorHidden)) {
    pal->xfixes.hide_cursor(pal->xcb.con, (XcbWindow)windowId);
    pal->flags |= GapPalFlags_CursorHidden;

  } else if (!hidden && pal->flags & GapPalFlags_CursorHidden) {
    pal->xfixes.show_cursor(pal->xcb.con, (XcbWindow)windowId);
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
    pal->xcb.change_property(
        pal->xcb.con,
        0 /* XCB_PROP_MODE_REPLACE */,
        (XcbWindow)windowId,
        pal->xcb.atomWmIcon,
        6 /* XCB_ATOM_CARDINAL */,
        sizeof(u32) * 8,
        (u32)(pal->icons[icon].size / sizeof(u32)),
        pal->icons[icon].ptr);
  } else {
    pal->xcb.delete_property(pal->xcb.con, (XcbWindow)windowId, pal->xcb.atomWmIcon);
  }

  window->icon = icon;
}

void gap_pal_window_cursor_set(GapPal* pal, const GapWindowId windowId, const GapCursor cursor) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  pal->xcb.change_window_attributes(
      pal->xcb.con, (XcbWindow)windowId, 16384 /* XCB_CW_CURSOR */, &pal->cursors[cursor]);

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
  pal->xcb.warp_pointer(pal->xcb.con, 0, (XcbWindow)windowId, 0, 0, 0, 0, xcbPos.x, xcbPos.y);

  pal_window((GapPal*)pal, windowId)->params[GapParam_CursorPos] = position;
}

void gap_pal_window_clip_copy(GapPal* pal, const GapWindowId windowId, const String value) {
  const usize maxLen = pal->xcb.maxRequestLen - 24 /* sizeof(xcb_change_property_request_t) */;
  if (value.size > maxLen) {
    // NOTE: Exceeding this limit would require splitting the data into chunks.
    log_w(
        "Clipboard copy request size exceeds limit",
        log_param("size", fmt_size(value.size)),
        log_param("limit", fmt_size(maxLen)));
    return;
  }

  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  string_maybe_free(g_allocHeap, window->clipCopy);
  window->clipCopy = string_dup(g_allocHeap, value);
  pal->xcb.set_selection_owner(pal->xcb.con, (XcbWindow)windowId, pal->xcb.atomClipboard, 0);
}

void gap_pal_window_clip_paste(GapPal* pal, const GapWindowId windowId) {
  pal->xcb.delete_property(pal->xcb.con, (XcbWindow)windowId, pal->xcb.atomVoloClipboard);
  pal->xcb.convert_selection(
      pal->xcb.con,
      (XcbWindow)windowId,
      pal->xcb.atomClipboard,
      pal->xcb.atomUtf8String,
      pal->xcb.atomVoloClipboard,
      0);
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

uptr gap_pal_native_app_handle(const GapPal* pal) { return (uptr)pal->xcb.con; }

void gap_pal_modal_error(const String message) {
  // NOTE: Can be called in parallel with any of the other apis (and itself).

  // TODO: Implement.
  (void)message;
}
