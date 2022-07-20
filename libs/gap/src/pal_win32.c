#include "core_diag.h"
#include "core_math.h"
#include "core_path.h"
#include "core_rng.h"
#include "core_thread.h"
#include "core_time.h"
#include "core_utf8.h"
#include "core_winutils.h"
#include "log_logger.h"

#include "pal_internal.h"

#include <windows.h>

#if !defined(__MINGW32__)
#include <shellscalingapi.h>
#define VOLO_WIN32_SHELL_SCALING_API
#endif

#define pal_window_min_width 128
#define pal_window_min_height 128
#define pal_window_default_dpi 96

ASSERT(sizeof(GapWindowId) >= sizeof(HWND), "GapWindowId should be able to represent a Win32 HWND")

static const DWORD g_winStyle           = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
static const DWORD g_winFullscreenStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

typedef struct {
  GapWindowId       id;
  Mem               className;
  GapVector         params[GapParam_Count];
  GapPalWindowFlags flags : 16;
  GapKeySet         keysPressed, keysPressedWithRepeat, keysReleased, keysDown;
  GapVector         lastWindowedPosition;
  bool              inModalLoop;
  DynString         inputText;
  String            clipPaste;
  GapCursor         cursor;
  u16               dpi;
} GapPalWindow;

typedef enum {
  GapPalFlags_CursorHidden   = 1 << 0,
  GapPalFlags_CursorCaptured = 1 << 1,
} GapPalFlags;

struct sGapPal {
  Allocator* alloc;
  DynArray   windows; // GapPalWindow[]

  HINSTANCE   moduleInstance;
  i64         owningThreadId;
  GapPalFlags flags;

  HCURSOR cursors[GapCursor_Count];
};

static void pal_check_thread_ownership(GapPal* pal) {
  if (g_thread_tid != pal->owningThreadId) {
    diag_crash_msg("Called from non-owning thread: {}", fmt_int(g_thread_tid));
  }
}

static void pal_crash_with_win32_err(String api) {
  const DWORD err = GetLastError();
  diag_crash_msg(
      "Win32 api call failed, api: {}, error: {}, {}",
      fmt_text(api),
      fmt_int((u64)err),
      fmt_text(winutils_error_msg_scratch(err)));
}

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

static void pal_dpi_init() {
  static bool g_initialized;
  if (!g_initialized) {
    // Mark the process as per-window dpi-aware.
#if defined(VOLO_WIN32_SHELL_SCALING_API)
    if (SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE) != S_OK) {
      diag_crash_msg("Failed to set win32 dpi awareness");
    }
#else
    if (!SetProcessDPIAware()) {
      diag_crash_msg("Failed to set win32 dpi awareness");
    }
#endif
    g_initialized = true;
  }
}

static void pal_cursors_init(GapPal* pal) {
  pal->cursors[GapCursor_Normal]     = LoadCursor(null, IDC_ARROW);
  pal->cursors[GapCursor_Click]      = LoadCursor(null, IDC_HAND);
  pal->cursors[GapCursor_Text]       = LoadCursor(null, IDC_IBEAM);
  pal->cursors[GapCursor_Busy]       = LoadCursor(null, IDC_WAIT);
  pal->cursors[GapCursor_Crosshair]  = LoadCursor(null, IDC_CROSS);
  pal->cursors[GapCursor_ResizeDiag] = LoadCursor(null, IDC_SIZENWSE);
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

static RECT pal_client_to_window_rect(
    const GapVector clientPosition, const GapVector clientSize, const DWORD style) {
  RECT rect = {
      .left   = (long)clientPosition.x,
      .top    = (long)clientPosition.y,
      .right  = (long)(clientPosition.x + clientSize.x),
      .bottom = (long)(clientPosition.y + clientSize.y),
  };
  if (!AdjustWindowRect(&rect, style, false)) {
    pal_crash_with_win32_err(string_lit("AdjustWindowRect"));
  }
  return rect;
}

static RECT pal_client_rect(const GapWindowId windowId) {
  RECT clientRect;
  if (!GetClientRect((HWND)windowId, &clientRect)) {
    pal_crash_with_win32_err(string_lit("GetClientRect"));
  }
  return clientRect;
}

static GapVector pal_client_to_screen(const GapWindowId windowId, const GapVector clientPosition) {
  POINT point = {.x = clientPosition.x, .y = clientPosition.y};
  if (!ClientToScreen((HWND)windowId, &point)) {
    pal_crash_with_win32_err(string_lit("ClientToScreen"));
  }
  return gap_vector((i32)point.x, (i32)point.y);
}

static GapVector pal_query_cursor_pos(const GapWindowId windowId) {
  POINT point;
  if (!GetCursorPos(&point)) {
    pal_crash_with_win32_err(string_lit("GetCursorPos"));
  }
  if (!ScreenToClient((HWND)windowId, &point)) {
    pal_crash_with_win32_err(string_lit("ScreenToClient"));
  }
  return gap_vector((i32)point.x, (i32)point.y);
}

static u16 pal_query_dpi(const GapWindowId windowId) {
#if defined(VOLO_WIN32_SHELL_SCALING_API)
  HMONITOR monitor = MonitorFromWindow((HWND)windowId, MONITOR_DEFAULTTONEAREST);
  if (UNLIKELY(!monitor)) {
    return pal_window_default_dpi;
  }
  UINT dpiX, dpiY;
  if (UNLIKELY(GetDpiForMonitor(monitor, MDT_RAW_DPI, &dpiX, &dpiY) != S_OK)) {
    pal_crash_with_win32_err(string_lit("GetDpiForMonitor"));
  }
  return (u16)dpiX;
#else
  (void)windowId;
  return pal_window_default_dpi;
#endif
}

static GapKey pal_win32_translate_key(const u8 scanCode) {
  switch (scanCode) {
  case 0x2A: // Left-shift.
  case 0x36: // Right-shift.
    return GapKey_Shift;
  case 0x1D:
    return GapKey_Control;
  case 0x0E:
    return GapKey_Backspace;
  case 0x53:
    return GapKey_Delete;
  case 0x0F:
    return GapKey_Tab;
  case 0x29:
    return GapKey_Tilde;
  case 0x1C:
    return GapKey_Return;
  case 0x01:
    return GapKey_Escape;
  case 0x39:
    return GapKey_Space;
  case 0x0D:
  case 0x4E: // Numpad +.
    return GapKey_Plus;
  case 0x0C:
  case 0x4A: // Numpad -.
    return GapKey_Minus;
  case 0x47:
    return GapKey_Home;
  case 0x4F:
    return GapKey_End;
  case 0x49:
    return GapKey_PageUp;
  case 0x51:
    return GapKey_PageDown;
  case 0x48:
    return GapKey_ArrowUp;
  case 0x50:
    return GapKey_ArrowDown;
  case 0x4D:
    return GapKey_ArrowRight;
  case 0x4B:
    return GapKey_ArrowLeft;

  case 0x1E:
    return GapKey_A;
  case 0x30:
    return GapKey_B;
  case 0x2E:
    return GapKey_C;
  case 0x20:
    return GapKey_D;
  case 0x12:
    return GapKey_E;
  case 0x21:
    return GapKey_F;
  case 0x22:
    return GapKey_G;
  case 0x23:
    return GapKey_H;
  case 0x17:
    return GapKey_I;
  case 0x24:
    return GapKey_J;
  case 0x25:
    return GapKey_K;
  case 0x26:
    return GapKey_L;
  case 0x32:
    return GapKey_M;
  case 0x31:
    return GapKey_N;
  case 0x18:
    return GapKey_O;
  case 0x19:
    return GapKey_P;
  case 0x10:
    return GapKey_Q;
  case 0x13:
    return GapKey_R;
  case 0x1F:
    return GapKey_S;
  case 0x14:
    return GapKey_T;
  case 0x16:
    return GapKey_U;
  case 0x2F:
    return GapKey_V;
  case 0x11:
    return GapKey_W;
  case 0x2D:
    return GapKey_X;
  case 0x15:
    return GapKey_Y;
  case 0x2C:
    return GapKey_Z;

  case 0x0B:
    return GapKey_Alpha0;
  case 0x02:
    return GapKey_Alpha1;
  case 0x03:
    return GapKey_Alpha2;
  case 0x04:
    return GapKey_Alpha3;
  case 0x05:
    return GapKey_Alpha4;
  case 0x06:
    return GapKey_Alpha5;
  case 0x07:
    return GapKey_Alpha6;
  case 0x08:
    return GapKey_Alpha7;
  case 0x09:
    return GapKey_Alpha8;
  case 0x0A:
    return GapKey_Alpha9;

  case 0x3B:
    return GapKey_F1;
  case 0x3C:
    return GapKey_F2;
  case 0x3D:
    return GapKey_F3;
  case 0x3E:
    return GapKey_F4;
  case 0x3F:
    return GapKey_F5;
  case 0x40:
    return GapKey_F6;
  case 0x41:
    return GapKey_F7;
  case 0x42:
    return GapKey_F8;
  case 0x43:
    return GapKey_F9;
  case 0x44:
    return GapKey_F10;
  case 0x57:
    return GapKey_F11;
  case 0x58:
    return GapKey_F12;
  }
  // log_d("Unrecognised win32 key", log_param("scancode", fmt_int(scanCode, .base = 16)));
  return GapKey_None;
}

static void pal_event_close(GapPalWindow* window) {
  window->flags |= GapPalWindowFlags_CloseRequested;
}

static void pal_event_focus_gained(GapPalWindow* window) {
  if (window->flags & GapPalWindowFlags_Focussed) {
    return;
  }
  window->flags |= GapPalWindowFlags_Focussed;
  window->flags |= GapPalWindowFlags_FocusGained;

  log_d("Window focus gained", log_param("id", fmt_int(window->id)));
}

static void pal_event_focus_lost(GapPalWindow* window) {
  if (!(window->flags & GapPalWindowFlags_Focussed)) {
    return;
  }

  window->flags &= ~GapPalWindowFlags_Focussed;
  window->flags |= GapPalWindowFlags_FocusLost;

  gap_keyset_clear(&window->keysDown);

  log_d("Window focus lost", log_param("id", fmt_int(window->id)));
}

static void pal_event_modal_loop_enter(GapPalWindow* window) { window->inModalLoop = true; }

static void pal_event_modal_loop_exit(GapPalWindow* window) {
  window->inModalLoop = false;
  if (window->flags & GapPalWindowFlags_Resized) {
    const GapVector newSize = window->params[GapParam_WindowSize];
    log_d(
        "Window resized",
        log_param("id", fmt_int(window->id)),
        log_param("size", gap_vector_fmt(newSize)));
  }
}

static void pal_event_resize(GapPalWindow* window, const GapVector newSize) {
  if (gap_vector_equal(window->params[GapParam_WindowSize], newSize)) {
    return;
  }
  window->params[GapParam_WindowSize] = newSize;
  window->flags |= GapPalWindowFlags_Resized;

  if (!window->inModalLoop) {
    log_d(
        "Window resized",
        log_param("id", fmt_int(window->id)),
        log_param("size", gap_vector_fmt(newSize)));
  }
}

MAYBE_UNUSED static void pal_event_dpi_changed(GapPalWindow* window, const u16 newDpi) {
  if (window->dpi == newDpi) {
    return;
  }
  window->dpi = newDpi;
  window->flags |= GapPalWindowFlags_DpiChanged;

  log_d(
      "Window dpi changed",
      log_param("id", fmt_int(window->id)),
      log_param("dpi", fmt_int(newDpi)));
}

static void pal_event_cursor(GapPalWindow* window, const GapVector newPos) {
  if (gap_vector_equal(window->params[GapParam_CursorPos], newPos)) {
    return;
  }

  /**
   * NOTE: Win32 uses top-left as the origin while the Volo project uses bottom-left, so we have to
   * remap the y coordinate.
   */

  window->params[GapParam_CursorPos] = (GapVector){
      .x = newPos.x,
      .y = window->params[GapParam_WindowSize].height - newPos.y,
  };
  window->flags |= GapPalWindowFlags_CursorMoved;
}

static void pal_event_press(GapPalWindow* window, const GapKey key) {
  if (key != GapKey_None) {
    gap_keyset_set(&window->keysPressedWithRepeat, key);
    if (!gap_keyset_test(&window->keysDown, key)) {
      gap_keyset_set(&window->keysPressed, key);
      gap_keyset_set(&window->keysDown, key);
    }
    window->flags |= GapPalWindowFlags_KeyPressed;
  }
}

static void pal_event_release(GapPalWindow* window, const GapKey key) {
  if (key != GapKey_None && gap_keyset_test(&window->keysDown, key)) {
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

static void pal_cursor_interaction_start(GapPalWindow* window) {
  /**
   * Enable cursor capture if its not already explicitly enabled with the
   * 'gap_pal_window_cursor_capture' api. This allows us to still get mouse events (like move and
   * release) even if you leave the window area while in an interaction.
   */
  if (!(window->flags & GapPalFlags_CursorCaptured)) {
    SetCapture((HWND)window->id);
  }
}

static void pal_cursor_interaction_end(GapPalWindow* window) {
  /**
   * Release the capture if its not explicitly requested using the 'gap_pal_window_cursor_capture'
   * api.
   */
  if (!(window->flags & GapPalFlags_CursorCaptured)) {
    ReleaseCapture();
  }
}

static bool
pal_event(GapPal* pal, const HWND wnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {
  GapPalWindow* window = pal_maybe_window(pal, (GapWindowId)wnd);
  if (!window) {
    /**
     * The window procedure is already invoked before the win32 CreateWindow call returns, so its
     * possible to get here without a window object being created yet.
     */
    return false;
  }
  switch (msg) {
  case WM_CLOSE:
    pal_event_close(window);
    return true;
  case WM_MOVE: {
    const GapVector newPos = gap_vector((i32)(short)LOWORD(lParam), (i32)(short)HIWORD(lParam));
    if (!(window->flags & GapPalWindowFlags_Fullscreen)) {
      window->lastWindowedPosition = newPos;
    }
    return true;
  }
  case WM_SETFOCUS: {
    pal_event_focus_gained(window);

    // Update the cursor as it was probably moved since we where focussed last.
    pal_event_cursor(window, pal_query_cursor_pos(window->id));
    return true;
  }
  case WM_KILLFOCUS: {
    pal_event_focus_lost(window);
    return true;
  }
  case WM_ENTERSIZEMOVE: {
    pal_event_modal_loop_enter(window);
    return true;
  }
  case WM_EXITSIZEMOVE:
  case WM_CAPTURECHANGED: {
    if (window->inModalLoop) {
      pal_event_modal_loop_exit(window);
    }
    return true;
  }
  case WM_SIZE: {
    const GapVector newSize = gap_vector(LOWORD(lParam), HIWORD(lParam));
    pal_event_resize(window, newSize);
    return true;
  }
  case WM_GETMINMAXINFO: {
    LPMINMAXINFO minMaxInfo      = (LPMINMAXINFO)lParam;
    minMaxInfo->ptMinTrackSize.x = pal_window_min_width;
    minMaxInfo->ptMinTrackSize.y = pal_window_min_height;
    return true;
  }
#if defined(VOLO_WIN32_SHELL_SCALING_API)
  case WM_DPICHANGED: {
    /**
     * NOTE: We're querying the actual raw display dpi instead of window's logical dpi. Reason is
     * that its much easier to get consistent cross-platform behaviour this way.
     */
    const u16 newDpi = pal_query_dpi(window->id);
    pal_event_dpi_changed(window, newDpi);
    return true;
  }
#endif
  case WM_PAINT:
    ValidateRect(wnd, null);
    return true;
  case WM_MOUSEMOVE: {
    const GapVector newPos = gap_vector((i32)(short)LOWORD(lParam), (i32)(short)HIWORD(lParam));
    pal_event_cursor(window, newPos);
    return true;
  }
  case WM_LBUTTONDOWN:
    pal_event_press(window, GapKey_MouseLeft);
    pal_cursor_interaction_start(window);
    return true;
  case WM_RBUTTONDOWN:
    pal_event_press(window, GapKey_MouseRight);
    pal_cursor_interaction_start(window);
    return true;
  case WM_MBUTTONDOWN:
    pal_event_press(window, GapKey_MouseMiddle);
    pal_cursor_interaction_start(window);
    return true;
  case WM_LBUTTONUP:
    pal_event_release(window, GapKey_MouseLeft);
    pal_cursor_interaction_end(window);
    return true;
  case WM_RBUTTONUP:
    pal_event_release(window, GapKey_MouseRight);
    pal_cursor_interaction_end(window);
    return true;
  case WM_MBUTTONUP:
    pal_event_release(window, GapKey_MouseMiddle);
    pal_cursor_interaction_end(window);
    return true;
  case WM_KEYDOWN: {
    const u8 scanCode = LOBYTE(HIWORD(lParam));
    pal_event_press(window, pal_win32_translate_key(scanCode));
    return true;
  }
  case WM_KEYUP: {
    const u8 scanCode = LOBYTE(HIWORD(lParam));
    pal_event_release(window, pal_win32_translate_key(scanCode));
    return true;
  }
  case WM_MOUSEWHEEL: {
    const i32 scrollY    = GET_WHEEL_DELTA_WPARAM(wParam);
    const i32 scrollSign = math_sign(scrollY);
    pal_event_scroll(
        window, gap_vector(0, math_max(1, math_abs(scrollY) / WHEEL_DELTA) * scrollSign));
    return true;
  }
  case WM_MOUSEHWHEEL: {
    const i32 scrollX    = GET_WHEEL_DELTA_WPARAM(wParam);
    const i32 scrollSign = math_sign(scrollX);
    pal_event_scroll(
        window, gap_vector(math_max(1, math_abs(scrollX) / WHEEL_DELTA) * scrollSign, 0));
    return true;
  }
  case WM_CHAR: {
    /**
     * WParam contains the utf-16 unicode value.
     * TODO: Figure out how to handle utf-16 surrogate pairs, should we resolve them at this level?
     */
    utf8_cp_write(&window->inputText, (Unicode)wParam);
    return true;
  }
  case WM_SETCURSOR: {
    if (window->cursor != GapCursor_Normal) {
      SetCursor(pal->cursors[window->cursor]);
      return true;
    }
    return false;
  }
  default:
    return false;
  }
}

static LRESULT
pal_window_proc(const HWND wnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {

  /**
   * When creating a window we pass it a pointer to the platform that created it, then when we
   * receive 'WM_NCCREATE' event we store that pointer to the window's 'GWLP_USERDATA' storage.
   * This way for every event we can lookup what platform should handle it.
   */

  if (msg == WM_NCCREATE) {
    CREATESTRUCT* createMsg = (CREATESTRUCT*)lParam;
    SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR)createMsg->lpCreateParams);
  } else {
    GapPal* pal = (GapPal*)GetWindowLongPtr(wnd, GWLP_USERDATA);
    if (pal && pal_event(pal, wnd, msg, wParam, lParam)) {
      return 0;
    }
  }

  // The event was not handled, fall back to the default handler.
  return DefWindowProc(wnd, msg, wParam, lParam);
}

GapPal* gap_pal_create(Allocator* alloc) {
  pal_dpi_init();

  HMODULE instance = GetModuleHandle(null);
  if (!instance) {
    pal_crash_with_win32_err(string_lit("GetModuleHandle"));
  }

  GapPal* pal = alloc_alloc_t(alloc, GapPal);
  *pal        = (GapPal){
             .alloc          = alloc,
             .windows        = dynarray_create_t(alloc, GapPalWindow, 4),
             .moduleInstance = instance,
             .owningThreadId = g_thread_tid,
  };
  pal_cursors_init(pal);

  MAYBE_UNUSED const GapVector screenSize =
      gap_vector(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

  log_i(
      "Win32 platform init",
      log_param("screen-size", gap_vector_fmt(screenSize)),
      log_param("owning-thread", fmt_int(pal->owningThreadId)));

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
  pal_check_thread_ownership(pal);

  // Clear volatile state, like the key-presses from the previous update.
  pal_clear_volatile(pal);

  // Handle all win32 messages in the buffer.
  MSG msg;
  while (PeekMessage(&msg, null, 0, 0, PM_REMOVE)) {
    if (UNLIKELY(msg.message == WM_QUIT)) {
      dynarray_for_t(&pal->windows, GapPalWindow, win) {
        win->flags |= GapPalWindowFlags_CloseRequested;
      }
      log_d("Win32 application quit requested");
    } else {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
}

GapWindowId gap_pal_window_create(GapPal* pal, GapVector size) {
  pal_check_thread_ownership(pal);

  // Generate a unique class name for the window and convert it to a wide-string.
  const String classNameUtf8 = fmt_write_scratch("volo_{}", fmt_int(rng_sample_u32(g_rng)));
  const Mem    className =
      alloc_dup(pal->alloc, winutils_to_widestr_scratch(classNameUtf8), alignof(wchar_t));

  const i32 screenWidth  = GetSystemMetrics(SM_CXSCREEN);
  const i32 screenHeight = GetSystemMetrics(SM_CYSCREEN);

  if (size.width <= 0) {
    size.width = screenWidth;
  } else if (size.width < pal_window_min_width) {
    size.width = pal_window_min_width;
  }
  if (size.height <= 0) {
    size.height = screenHeight;
  } else if (size.height < pal_window_min_height) {
    size.height = pal_window_min_height;
  }

  const WNDCLASSEX winClass = {
      .cbSize        = sizeof(WNDCLASSEX),
      .style         = CS_HREDRAW | CS_VREDRAW,
      .lpfnWndProc   = pal_window_proc,
      .hInstance     = pal->moduleInstance,
      .hIcon         = LoadIcon(null, IDI_APPLICATION),
      .hCursor       = LoadCursor(null, IDC_ARROW),
      .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
      .lpszClassName = className.ptr,
      .hIconSm       = LoadIcon(null, IDI_WINLOGO),
  };

  if (!RegisterClassEx(&winClass)) {
    pal_crash_with_win32_err(string_lit("RegisterClassEx"));
  }

  const GapVector position = gap_vector((screenWidth - size.x) / 2, (screenHeight - size.y) / 2);
  const RECT      desiredWindowRect = pal_client_to_window_rect(position, size, g_winStyle);
  const HWND      windowHandle      = CreateWindow(
      className.ptr,
      null,
      g_winStyle,
      desiredWindowRect.left,
      desiredWindowRect.top,
      desiredWindowRect.right - desiredWindowRect.left,
      desiredWindowRect.bottom - desiredWindowRect.top,
      null,
      null,
      pal->moduleInstance,
      (void*)pal);

  if (!windowHandle) {
    pal_crash_with_win32_err(string_lit("CreateWindow"));
  }

  ShowWindow(windowHandle, SW_SHOW);
  SetForegroundWindow(windowHandle);
  SetFocus(windowHandle);

  const GapWindowId id             = (GapWindowId)windowHandle;
  const RECT        realClientRect = pal_client_rect(id);
  const GapVector   realClientSize = gap_vector(
      realClientRect.right - realClientRect.left, realClientRect.bottom - realClientRect.top);
  const u16 dpi = pal_query_dpi(id);

  *dynarray_push_t(&pal->windows, GapPalWindow) = (GapPalWindow){
      .id                          = id,
      .className                   = className,
      .params[GapParam_WindowSize] = realClientSize,
      .flags                       = GapPalWindowFlags_Focussed | GapPalWindowFlags_FocusGained,
      .lastWindowedPosition        = position,
      .inputText                   = dynstring_create(g_alloc_heap, 64),
      .dpi                         = dpi,
  };

  log_i(
      "Window created",
      log_param("id", fmt_int(id)),
      log_param("size", gap_vector_fmt(realClientSize)),
      log_param("dpi", fmt_int(dpi)));

  return id;
}

void gap_pal_window_destroy(GapPal* pal, const GapWindowId windowId) {
  pal_check_thread_ownership(pal);

  if (!DestroyWindow((HWND)windowId)) {
    pal_crash_with_win32_err(string_lit("DestroyWindow"));
  }
  for (usize i = 0; i != pal->windows.size; ++i) {
    GapPalWindow* window = dynarray_at_t(&pal->windows, i, GapPalWindow);
    if (window->id == windowId) {
      if (!UnregisterClass(window->className.ptr, pal->moduleInstance)) {
        pal_crash_with_win32_err(string_lit("UnregisterClass"));
      }
      alloc_free(pal->alloc, window->className);
      dynstring_destroy(&window->inputText);
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
  pal_check_thread_ownership(pal);

  const usize wideTitleBytes = winutils_to_widestr_size(title);
  if (UNLIKELY(wideTitleBytes > usize_kibibyte)) {
    log_w(
        "Window title size exceeds limit",
        log_param("size", fmt_size(wideTitleBytes)),
        log_param("limit", fmt_size(usize_kibibyte)));
    return;
  }

  const Mem buffer = mem_stack(wideTitleBytes);
  winutils_to_widestr(buffer, title);

  if (!SetWindowText((HWND)windowId, (const wchar_t*)buffer.ptr)) {
    pal_crash_with_win32_err(string_lit("SetWindowText"));
  }
}

void gap_pal_window_resize(
    GapPal* pal, const GapWindowId windowId, GapVector size, const bool fullscreen) {
  pal_check_thread_ownership(pal);

  GapPalWindow* window = pal_window((GapPal*)pal, windowId);

  if (size.width <= 0) {
    size.width = GetSystemMetrics(SM_CXSCREEN);
  } else if (size.width < pal_window_min_width) {
    size.width = pal_window_min_width;
  }

  if (size.height <= 0) {
    size.height = GetSystemMetrics(SM_CYSCREEN);
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
    SetWindowLongPtr((HWND)windowId, GWL_STYLE, g_winFullscreenStyle);
    ShowWindow((HWND)windowId, SW_MAXIMIZE);

  } else {
    window->flags &= ~GapPalWindowFlags_Fullscreen;

    SetWindowLongPtr((HWND)windowId, GWL_STYLE, g_winStyle);

    const RECT rect = pal_client_to_window_rect(window->lastWindowedPosition, size, g_winStyle);
    if (!SetWindowPos(
            (HWND)windowId,
            null,
            rect.left,
            rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top,
            SWP_NOCOPYBITS | SWP_NOZORDER | SWP_NOREDRAW | SWP_FRAMECHANGED | SWP_SHOWWINDOW)) {
      pal_crash_with_win32_err(string_lit("SetWindowPos"));
    }
  }
}

void gap_pal_window_cursor_hide(GapPal* pal, const GapWindowId windowId, const bool hidden) {
  pal_check_thread_ownership(pal);
  (void)windowId;

  if (hidden && !(pal->flags & GapPalFlags_CursorHidden)) {
    ShowCursor(false);
    pal->flags |= GapPalFlags_CursorHidden;
  } else if (!hidden && pal->flags & GapPalFlags_CursorHidden) {
    ShowCursor(true);
    pal->flags &= ~GapPalFlags_CursorHidden;
  }
}

void gap_pal_window_cursor_capture(GapPal* pal, const GapWindowId windowId, const bool captured) {
  pal_check_thread_ownership(pal);

  if (captured && !(pal->flags & GapPalFlags_CursorCaptured)) {
    SetCapture((HWND)windowId);
    pal->flags |= GapPalFlags_CursorCaptured;
  } else if (!captured && pal->flags & GapPalFlags_CursorCaptured) {
    ReleaseCapture();
    pal->flags &= ~GapPalFlags_CursorCaptured;
  }
}

void gap_pal_window_cursor_set(GapPal* pal, const GapWindowId windowId, const GapCursor cursor) {
  pal_maybe_window(pal, windowId)->cursor = cursor;
}

void gap_pal_window_cursor_pos_set(
    GapPal* pal, const GapWindowId windowId, const GapVector position) {
  pal_check_thread_ownership(pal);

  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  /**
   * NOTE: Win32 uses top-left as the origin while the Volo project uses bottom-left, so we have to
   * remap the y coordinate.
   */
  const GapVector win32Pos = {
      .x = position.x,
      .y = window->params[GapParam_WindowSize].height - position.y,
  };

  const GapVector screenPos = pal_client_to_screen(windowId, win32Pos);
  if (!SetCursorPos(screenPos.x, screenPos.y)) {
    pal_crash_with_win32_err(string_lit("SetCursorPos"));
  }
  pal_window((GapPal*)pal, windowId)->params[GapParam_CursorPos] = position;
}

void gap_pal_window_clip_copy(GapPal* pal, const GapWindowId windowId, const String value) {
  (void)pal;

  if (!OpenClipboard((HWND)windowId)) {
    pal_crash_with_win32_err(string_lit("OpenClipboard"));
  }
  if (!EmptyClipboard()) {
    pal_crash_with_win32_err(string_lit("EmptyClipboard"));
  }
  /**
   * Allocate a movable global memory object for the data and copy the value into it.
   * NOTE: Converts the value from utf8 to utf16 for compatiblity with Win32's 'UNICODETEXT' format.
   * TODO: We should convert '\n' to '\r\n' for compatiblity with other Win32 applications.
   */
  const usize wcharByteSize = winutils_to_widestr_size(value);
  if (sentinel_check(wcharByteSize)) {
    goto Done; // Input is invalid utf8. TODO: Crash with error?
  }
  const HGLOBAL clipMemAlloc = GlobalAlloc(GMEM_MOVEABLE, wcharByteSize);
  if (!clipMemAlloc) {
    goto Done; // Allocation failed. TODO: Crash with error?
  }
  void* clipMemPtr = GlobalLock(clipMemAlloc); // Lock the moveable allocation for copying.
  winutils_to_widestr(mem_create(clipMemPtr, wcharByteSize), value);
  GlobalUnlock(clipMemAlloc);

  if (!SetClipboardData(CF_UNICODETEXT, clipMemAlloc)) {
    pal_crash_with_win32_err(string_lit("SetClipboardData"));
  }
Done:
  if (!CloseClipboard()) {
    pal_crash_with_win32_err(string_lit("CloseClipboard"));
  }
}

void gap_pal_window_clip_paste(GapPal* pal, const GapWindowId windowId) {
  GapPalWindow* window = pal_maybe_window(pal, windowId);
  diag_assert(window);

  string_maybe_free(g_alloc_heap, window->clipPaste);
  window->clipPaste = string_empty;

  if (!OpenClipboard((HWND)windowId)) {
    pal_crash_with_win32_err(string_lit("OpenClipboard"));
  }
  const HGLOBAL clipMemAlloc = GetClipboardData(CF_UNICODETEXT);
  if (!clipMemAlloc) {
    goto Done; // No clipboard data available.
  }
  /**
   * Copy the data out of the (potentially moveable) global memory object.
   * NOTE: Converts the value from utf16 to utf8 for compatiblity with Win32's 'UNICODETEXT' format.
   */
  void*       clipMemPtr = GlobalLock(clipMemAlloc); // Lock the moveable allocation for copying.
  const usize wcharCount = wcslen((const wchar_t*)clipMemPtr);
  const usize stringSize = winutils_from_widestr_size(clipMemPtr, wcharCount);
  window->clipPaste      = alloc_alloc(g_alloc_heap, stringSize, 1);
  winutils_from_widestr(window->clipPaste, clipMemPtr, wcharCount);
  GlobalUnlock(clipMemAlloc);

  window->flags |= GapPalWindowFlags_ClipPaste;

Done:
  if (!CloseClipboard()) {
    pal_crash_with_win32_err(string_lit("CloseClipboard"));
  }
}

String gap_pal_window_clip_paste_result(GapPal* pal, const GapWindowId windowId) {
  return pal_maybe_window(pal, windowId)->clipPaste;
}

u16 gap_pal_window_dpi(GapPal* pal, const GapWindowId windowId) {
  return pal_maybe_window(pal, windowId)->dpi;
}

TimeDuration gap_pal_doubleclick_interval() {
  const UINT doubleClickMilliseconds = GetDoubleClickTime();
  return time_milliseconds(doubleClickMilliseconds);
}

GapNativeWm gap_pal_native_wm() { return GapNativeWm_Win32; }

uptr gap_pal_native_app_handle(const GapPal* pal) { return (uptr)pal->moduleInstance; }
