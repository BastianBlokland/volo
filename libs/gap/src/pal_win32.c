#include "core_diag.h"
#include "core_math.h"
#include "core_path.h"
#include "core_rng.h"
#include "core_thread.h"
#include "core_winutils.h"
#include "log_logger.h"

#include "pal_internal.h"

#include <windows.h>

#define pal_window_min_width 128
#define pal_window_min_height 128

ASSERT(sizeof(GapWindowId) >= sizeof(HWND), "GapWindowId should be able to represent a Win32 HWND")

static const DWORD g_winStyle           = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
static const DWORD g_winFullscreenStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

typedef struct {
  GapWindowId       id;
  Mem               className;
  GapVector         params[GapParam_Count];
  GapPalWindowFlags flags : 16;
  GapKeySet         keysPressed, keysReleased, keysDown;
  GapVector         lastWindowedPosition;
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

static void pal_clear_volatile(GapPal* pal) {
  dynarray_for_t(&pal->windows, GapPalWindow, window) {
    gap_keyset_clear(&window->keysPressed);
    gap_keyset_clear(&window->keysReleased);

    window->params[GapParam_ScrollDelta] = gap_vector(0, 0);

    window->flags &= ~GapPalWindowFlags_Volatile;
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

static GapKey pal_win32_translate_key(const WPARAM key) {
  switch (key) {
  case VK_SHIFT:
    return GapKey_Shift;
  case VK_CONTROL:
    return GapKey_Control;
  case VK_BACK:
    return GapKey_Backspace;
  case VK_DELETE:
    return GapKey_Delete;
  case VK_TAB:
    return GapKey_Tab;
  case VK_OEM_3:
    return GapKey_Tilde;
  case VK_RETURN:
    return GapKey_Return;
  case VK_ESCAPE:
    return GapKey_Escape;
  case VK_SPACE:
    return GapKey_Space;
  case VK_OEM_PLUS:
    return GapKey_Plus;
  case VK_OEM_MINUS:
    return GapKey_Minus;
  case VK_UP:
    return GapKey_ArrowUp;
  case VK_DOWN:
    return GapKey_ArrowDown;
  case VK_RIGHT:
    return GapKey_ArrowRight;
  case VK_LEFT:
    return GapKey_ArrowLeft;

  case 0x41: // VK_A
    return GapKey_A;
  case 0x42: // VK_B
    return GapKey_B;
  case 0x43: // VK_C
    return GapKey_C;
  case 0x44: // VK_D
    return GapKey_D;
  case 0x45: // VK_E
    return GapKey_E;
  case 0x46: // VK_F
    return GapKey_F;
  case 0x47: // VK_G
    return GapKey_G;
  case 0x48: // VK_H
    return GapKey_H;
  case 0x49: // VK_I
    return GapKey_I;
  case 0x4A: // VK_J
    return GapKey_J;
  case 0x4B: // VK_K
    return GapKey_K;
  case 0x4C: // VK_L
    return GapKey_L;
  case 0x4D: // VK_M
    return GapKey_M;
  case 0x4E: // VK_N
    return GapKey_N;
  case 0x4F: // VK_O
    return GapKey_O;
  case 0x50: // VK_P
    return GapKey_P;
  case 0x51: // VK_Q
    return GapKey_Q;
  case 0x52: // VK_R
    return GapKey_R;
  case 0x53: // VK_S
    return GapKey_S;
  case 0x54: // VK_T
    return GapKey_T;
  case 0x55: // VK_U
    return GapKey_U;
  case 0x56: // VK_V
    return GapKey_V;
  case 0x57: // VK_W
    return GapKey_W;
  case 0x58: // VK_X
    return GapKey_X;
  case 0x59: // VK_Y
    return GapKey_Y;
  case 0x5A: // VK_Z
    return GapKey_Z;

  case 0x30: // VK_0
    return GapKey_Alpha0;
  case 0x31: // VK_1
    return GapKey_Alpha1;
  case 0x32: // VK_2
    return GapKey_Alpha2;
  case 0x33: // VK_3
    return GapKey_Alpha3;
  case 0x34: // VK_4
    return GapKey_Alpha4;
  case 0x35: // VK_5
    return GapKey_Alpha5;
  case 0x36: // VK_6
    return GapKey_Alpha6;
  case 0x37: // VK_7
    return GapKey_Alpha7;
  case 0x38: // VK_8
    return GapKey_Alpha8;
  case 0x39: // VK_9
    return GapKey_Alpha9;

  case VK_F1:
    return GapKey_F1;
  case VK_F2:
    return GapKey_F2;
  case VK_F3:
    return GapKey_F3;
  case VK_F4:
    return GapKey_F4;
  case VK_F5:
    return GapKey_F5;
  case VK_F6:
    return GapKey_F6;
  case VK_F7:
    return GapKey_F7;
  case VK_F8:
    return GapKey_F8;
  case VK_F9:
    return GapKey_F9;
  case VK_F10:
    return GapKey_F10;
  case VK_F11:
    return GapKey_F11;
  case VK_F12:
    return GapKey_F12;
  }
  // log_d("Unrecognised win32 key", log_param("keycode", fmt_int(key, .base = 16)));
  return GapKey_None;
}

static void pal_event_close(GapPalWindow* window) {
  window->flags |= GapPalWindowFlags_CloseRequested;
}

static void pal_event_resize(GapPalWindow* window, const GapVector newSize) {
  if (gap_vector_equal(window->params[GapParam_WindowSize], newSize)) {
    return;
  }
  window->params[GapParam_WindowSize] = newSize;
  window->flags |= GapPalWindowFlags_Resized;

  log_d("Window resized", log_param("size", gap_vector_fmt(newSize)));
}

static void pal_event_cursor(GapPalWindow* window, const GapVector newPos) {
  if (gap_vector_equal(window->params[GapParam_CursorPos], newPos)) {
    return;
  }
  window->params[GapParam_CursorPos] = newPos;
  window->flags |= GapPalWindowFlags_CursorMoved;
}

static void pal_event_press(GapPalWindow* window, const GapKey key) {
  if (key != GapKey_None && !gap_keyset_test(&window->keysDown, key)) {
    gap_keyset_set(&window->keysPressed, key);
    gap_keyset_set(&window->keysDown, key);
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
    return true;
  case WM_RBUTTONDOWN:
    pal_event_press(window, GapKey_MouseRight);
    return true;
  case WM_MBUTTONDOWN:
    pal_event_press(window, GapKey_MouseMiddle);
    return true;
  case WM_LBUTTONUP:
    pal_event_release(window, GapKey_MouseLeft);
    return true;
  case WM_RBUTTONUP:
    pal_event_release(window, GapKey_MouseRight);
    return true;
  case WM_MBUTTONUP:
    pal_event_release(window, GapKey_MouseMiddle);
    return true;
  case WM_KEYDOWN:
    pal_event_press(window, pal_win32_translate_key(wParam));
    return true;
  case WM_KEYUP:
    pal_event_release(window, pal_win32_translate_key(wParam));
    return true;
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
    TranslateMessage(&msg);
    DispatchMessage(&msg);
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

  *dynarray_push_t(&pal->windows, GapPalWindow) = (GapPalWindow){
      .id                          = id,
      .className                   = className,
      .params[GapParam_WindowSize] = realClientSize,
      .lastWindowedPosition        = position,
  };

  log_i(
      "Window created",
      log_param("id", fmt_int(id)),
      log_param("size", gap_vector_fmt(realClientSize)));

  return id;
}

void gap_pal_window_destroy(GapPal* pal, const GapWindowId windowId) {

  const bool destroyNativeResources = g_thread_tid == pal->owningThreadId;
  if (destroyNativeResources) {
    if (!DestroyWindow((HWND)windowId)) {
      pal_crash_with_win32_err(string_lit("DestroyWindow"));
    }
  } else {
    log_w(
        "Failed to destroy win32 window resources",
        log_param("id", fmt_int(windowId)),
        log_param("reason", fmt_text_lit("Destroyed from non-owning thread")));
  }

  for (usize i = 0; i != pal->windows.size; ++i) {
    GapPalWindow* window = dynarray_at_t(&pal->windows, i, GapPalWindow);
    if (window->id == windowId) {
      if (destroyNativeResources) {
        if (!UnregisterClass(window->className.ptr, pal->moduleInstance)) {
          pal_crash_with_win32_err(string_lit("UnregisterClass"));
        }
      }
      alloc_free(pal->alloc, window->className);
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

void gap_pal_window_cursor_set(GapPal* pal, const GapWindowId windowId, const GapVector position) {
  pal_check_thread_ownership(pal);

  const GapVector screenPos = pal_client_to_screen(windowId, position);
  if (!SetCursorPos(screenPos.x, screenPos.y)) {
    pal_crash_with_win32_err(string_lit("SetCursorPos"));
  }
  pal_window((GapPal*)pal, windowId)->params[GapParam_CursorPos] = position;
}

GapNativeWm gap_pal_native_wm() { return GapNativeWm_Win32; }

uptr gap_pal_native_app_handle(const GapPal* pal) { return (uptr)pal->moduleInstance; }
