#pragma once
#include "core_alloc.h"

typedef struct sRendContextVk RendContextVk;

RendContextVk* rend_vk_context_create(Allocator*);
void           rend_vk_context_destroy(RendContextVk*);
