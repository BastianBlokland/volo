#pragma once
#include "core_alloc.h"
#include "core_string.h"

typedef u32 GAppWindowId;

typedef struct sGAppPal GAppPal;

GAppPal*     gapp_pal_create(Allocator*);
void         gapp_pal_destroy(GAppPal*);
GAppWindowId gapp_pal_window_create(GAppPal*, u32 width, u32 height);
void         gapp_pal_window_destroy(GAppPal*, GAppWindowId);
