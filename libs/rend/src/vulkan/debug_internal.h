#pragma once
#include "vulkan_internal.h"

typedef enum {
  RendVkDebugFlags_None    = 0,
  RendVkDebugFlags_Verbose = 1 << 0,
} RendVkDebugFlags;

typedef struct sRendVkDebug RendVkDebug;

RendVkDebug* rend_vk_debug_create(VkInstance, VkAllocationCallbacks*, RendVkDebugFlags);
void         rend_vk_debug_destroy(RendVkDebug*);
