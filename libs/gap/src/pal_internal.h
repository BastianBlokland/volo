#pragma once
#include "core_alloc.h"
#include "core_string.h"
#include "gap_vector.h"

typedef u32 GapWindowId;

typedef enum {
  GapPalWindowFlags_CloseRequested = 1 << 0,
  GapPalWindowFlags_Resized        = 1 << 1,
  GapPalWindowFlags_CursorMoved    = 1 << 2,

  GapPalWindowFlags_Volatile =
      GapPalWindowFlags_CloseRequested | GapPalWindowFlags_Resized | GapPalWindowFlags_CursorMoved,
} GapPalWindowFlags;

typedef struct sGapPal GapPal;

GapPal*           gap_pal_create(Allocator*);
void              gap_pal_destroy(GapPal*);
void              gap_pal_update(GapPal*);
GapWindowId       gap_pal_window_create(GapPal*, GapVector size);
void              gap_pal_window_destroy(GapPal*, GapWindowId);
GapPalWindowFlags gap_pal_window_flags(const GapPal*, GapWindowId);
GapVector         gap_pal_window_size(const GapPal*, GapWindowId);
GapVector         gap_pal_window_cursor(const GapPal*, GapWindowId);
void              gap_pal_window_title_set(GapPal*, GapWindowId, String);
