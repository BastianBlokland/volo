#pragma once
#include "core_alloc.h"
#include "core_annotation.h"
#include "core_string.h"
#include "core_time.h"
#include "gap_cursor.h"
#include "gap_native.h"
#include "gap_vector.h"

#include "input_internal.h"

typedef u64 GapWindowId;

ASSERT(sizeof(GapWindowId) >= sizeof(uptr), "GapWindowId needs to be capable of storing pointers")

typedef enum {
  GapPalWindowFlags_CloseRequested = 1 << 0,
  GapPalWindowFlags_Resized        = 1 << 1,
  GapPalWindowFlags_CursorMoved    = 1 << 2,
  GapPalWindowFlags_Scrolled       = 1 << 3,
  GapPalWindowFlags_KeyPressed     = 1 << 4,
  GapPalWindowFlags_KeyReleased    = 1 << 5,
  GapPalWindowFlags_Fullscreen     = 1 << 6,
  GapPalWindowFlags_Focussed       = 1 << 7,
  GapPalWindowFlags_FocusLost      = 1 << 8,
  GapPalWindowFlags_FocusGained    = 1 << 9,
  GapPalWindowFlags_ClipPaste      = 1 << 10,
  GapPalWindowFlags_DpiChanged     = 1 << 11,

  GapPalWindowFlags_Volatile = GapPalWindowFlags_CloseRequested | GapPalWindowFlags_Resized |
                               GapPalWindowFlags_CursorMoved | GapPalWindowFlags_Scrolled |
                               GapPalWindowFlags_KeyPressed | GapPalWindowFlags_KeyReleased |
                               GapPalWindowFlags_FocusLost | GapPalWindowFlags_FocusGained |
                               GapPalWindowFlags_ClipPaste | GapPalWindowFlags_DpiChanged,
} GapPalWindowFlags;

typedef struct sGapPal GapPal;

GapPal*           gap_pal_create(Allocator*);
void              gap_pal_destroy(GapPal*);
void              gap_pal_update(GapPal*);
GapWindowId       gap_pal_window_create(GapPal*, GapVector size);
void              gap_pal_window_destroy(GapPal*, GapWindowId);
GapPalWindowFlags gap_pal_window_flags(const GapPal*, GapWindowId);
GapVector         gap_pal_window_param(const GapPal*, GapWindowId, GapParam);
const GapKeySet*  gap_pal_window_keys_pressed(const GapPal*, GapWindowId);
const GapKeySet*  gap_pal_window_keys_pressed_with_repeat(const GapPal*, GapWindowId);
const GapKeySet*  gap_pal_window_keys_released(const GapPal*, GapWindowId);
const GapKeySet*  gap_pal_window_keys_down(const GapPal*, GapWindowId);
String            gap_pal_window_input_text(const GapPal*, GapWindowId);
void              gap_pal_window_title_set(GapPal*, GapWindowId, String);
void              gap_pal_window_resize(GapPal*, GapWindowId, GapVector size, bool fullscreen);
void              gap_pal_window_cursor_hide(GapPal*, GapWindowId, bool hidden);
void              gap_pal_window_cursor_capture(GapPal*, GapWindowId, bool captured);
void              gap_pal_window_cursor_confine(GapPal*, GapWindowId, bool confined);
void              gap_pal_window_cursor_set(GapPal*, GapWindowId, GapCursor);
void              gap_pal_window_cursor_pos_set(GapPal*, GapWindowId, GapVector position);
void              gap_pal_window_clip_copy(GapPal*, GapWindowId, String value);
void              gap_pal_window_clip_paste(GapPal*, GapWindowId);
String            gap_pal_window_clip_paste_result(GapPal*, GapWindowId);
u16               gap_pal_window_dpi(GapPal*, GapWindowId);

TimeDuration gap_pal_doubleclick_interval(void);

GapNativeWm gap_pal_native_wm(void);
uptr        gap_pal_native_app_handle(const GapPal*);
