#pragma once
#include "core_alloc.h"

typedef struct sRendVkPlatform RendVkPlatform;

RendVkPlatform* rend_vk_platform_create();
void            rend_vk_platform_destroy(RendVkPlatform*);
