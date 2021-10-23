#pragma once
#include "core_types.h"
#include "ecs_module.h"

/**
 * Header for retrieving platform specific handles window-manager components.
 */

// Forward declare from 'gap_window.h'.
ecs_comp_extern(GapWindowComp);

/**
 * Identifier of a Window-Manager connection.
 */
typedef enum {
  GapNativeWm_Xcb,
  GapNativeWm_Win32,
} GapNativeWm;

/**
 * Check the window-manager connection type.
 */
GapNativeWm gap_native_wm();

/**
 * Retrieve the native handle to the given window.
 *
 * Per window-manager connection meaning:
 * - Xcb: The 'xcb_window_t' used in the connection to the x11 server.
 * - Win32: The 'HWND' handle of the window.
 */
uptr gap_native_window_handle(const GapWindowComp*);

/**
 * Retrieve the native handle to the application that the given window belongs to.
 *
 * Per window-manager connection meaning:
 * - Xcb: The 'xcb_connection_t*' connection to the x11 server
 * - Win32: The 'HINSTANCE' that owns the given window.
 */
uptr gap_native_app_handle(const GapWindowComp*);
