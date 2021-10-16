#pragma once
#include "core_alloc.h"

typedef struct sGAppPal GAppPal;

GAppPal* gapp_pal_create(Allocator*);
void     gapp_pal_destroy(GAppPal*);
