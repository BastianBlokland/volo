#pragma once
#include "core_alloc.h"

typedef struct sRendVkContext RendVkContext;

RendVkContext* rend_vk_context_create();
void           rend_vk_context_destroy(RendVkContext*);
