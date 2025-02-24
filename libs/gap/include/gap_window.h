#pragma once
#include "ecs_module.h"
#include "gap_icon.h"
#include "gap_input.h"
#include "gap_vector.h"

/**
 * Notification flags for events that occurred this tick.
 */
typedef enum eGapWindowEvents {
  GapWindowEvents_Initializing       = 1 << 0,
  GapWindowEvents_Resized            = 1 << 1,
  GapWindowEvents_KeyPressed         = 1 << 2,
  GapWindowEvents_KeyReleased        = 1 << 3,
  GapWindowEvents_TitleUpdated       = 1 << 4,
  GapWindowEvents_CloseRequested     = 1 << 5,
  GapWindowEvents_FocusGained        = 1 << 6, // Did the window gain focus this tick.
  GapWindowEvents_FocusLost          = 1 << 7, // Did the window lose focus this tick.
  GapWindowEvents_Focussed           = 1 << 8, // Does the window have focus this tick.
  GapWindowEvents_ClipPaste          = 1 << 9, // Was a value pasted from the clipboard this tick.
  GapWindowEvents_RefreshRateChanged = 1 << 10,
  GapWindowEvents_DpiChanged         = 1 << 11,
} GapWindowEvents;

/**
 * Configuration flags for setting up the desired window behavior.
 */
typedef enum eGapWindowFlags {
  GapWindowFlags_None             = 0,
  GapWindowFlags_CloseOnInterrupt = 1 << 0,
  GapWindowFlags_CloseOnRequest   = 1 << 1,
  GapWindowFlags_CursorHide       = 1 << 2,
  GapWindowFlags_CursorLock       = 1 << 3,
  GapWindowFlags_CursorConfine    = 1 << 4,
  GapWindowFlags_DefaultTitle     = 1 << 5,

  GapWindowFlags_Default =
      GapWindowFlags_CloseOnInterrupt | GapWindowFlags_CloseOnRequest | GapWindowFlags_DefaultTitle,
} GapWindowFlags;

typedef enum eGapWindowMode {
  GapWindowMode_Windowed   = 0,
  GapWindowMode_Fullscreen = 1,
} GapWindowMode;

/**
 * Ecs component for a window.
 */
ecs_comp_extern(GapWindowComp);
ecs_comp_extern_public(GapWindowAspectComp) { f32 ratio; /* width / height */ };

/**
 * Create a new window with the given size.
 */
EcsEntityId
gap_window_create(EcsWorld*, GapWindowMode, GapWindowFlags, GapVector size, GapIcon, String title);

/**
 * Close a currently open window.
 * NOTE: Will destroy the window entity but might be deferred a few ticks.
 */
void gap_window_close(GapWindowComp*);

GapWindowFlags gap_window_flags(const GapWindowComp*);
void           gap_window_flags_set(GapWindowComp*, GapWindowFlags);
void           gap_window_flags_unset(GapWindowComp*, GapWindowFlags);

/**
 * Retrieve the events that occurred this tick.
 */
GapWindowEvents gap_window_events(const GapWindowComp*);

/**
 * Retrieve the current window mode.
 */
GapWindowMode gap_window_mode(const GapWindowComp*);

/**
 * Request for the window to be resized.
 * NOTE: The actual resize operation might be deferred a few ticks.
 */
void gap_window_resize(GapWindowComp*, GapVector size, GapWindowMode);

/**
 * Retrieve the current window title.
 * NOTE: String will be invalidated when the title is updated.
 */
String gap_window_title_get(const GapWindowComp*);

/**
 * Request the window title to be updated.
 */
void gap_window_title_set(GapWindowComp*, String newTitle);

/**
 * Retrieve the current value of a parameter on the window.
 */
GapVector gap_window_param(const GapWindowComp*, GapParam);

/**
 * Test if the given key was pressed this tick.
 */
bool gap_window_key_pressed(const GapWindowComp*, GapKey);
bool gap_window_key_pressed_with_repeat(const GapWindowComp*, GapKey);

/**
 * Test if the given key was released this tick.
 */
bool gap_window_key_released(const GapWindowComp*, GapKey);

/**
 * Test if the given key is currently being held down.
 */
bool gap_window_key_down(const GapWindowComp*, GapKey);

/**
 * Update the window icon.
 */
void gap_window_icon_set(GapWindowComp*, GapIcon);

/**
 * Update the window cursor.
 */
void gap_window_cursor_set(GapWindowComp*, GapCursor);

/**
 * Retrieve the text that was entered this tick.
 * NOTE: Takes the user's keyboard layout into account.
 */
String gap_window_input_text(const GapWindowComp*);

/**
 * Copy a value to the clipboard.
 */
void gap_window_clip_copy(GapWindowComp*, String value);

/**
 * Paste a value from the clipboard.
 * NOTE: This is an asynchronous operation, after placing a paste request the 'ClipPaste' event will
 * be raised when a value has been retrieved from the clipboard.
 */
void   gap_window_clip_paste(GapWindowComp*);
String gap_window_clip_paste_result(const GapWindowComp*);

/**
 * Retrieve the name of the display that is currently showing the given window.
 */
String gap_window_display_name(const GapWindowComp*);

/**
 * Retrieve the window's current display refresh-rate.
 */
f32 gap_window_refresh_rate(const GapWindowComp*);

/**
 * Retrieve the window's current display density in 'Dots Per Inch'.
 */
u16 gap_window_dpi(const GapWindowComp*);

/**
 * Retrieve the system's double click interval.
 */
TimeDuration gap_window_doubleclick_interval(const GapWindowComp*);
