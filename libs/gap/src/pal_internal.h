#pragma once
#include "core_alloc.h"
#include "core_string.h"

typedef u32 GapWindowId;

typedef struct sGapPal GapPal;

GapPal*     gap_pal_create(Allocator*);
void        gap_pal_destroy(GapPal*);
GapWindowId gap_pal_window_create(GapPal*, u32 width, u32 height);
void        gap_pal_window_destroy(GapPal*, GapWindowId);
void        gap_pal_window_title_set(GapPal*, GapWindowId, String);
