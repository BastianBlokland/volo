#pragma once
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

VkPipelineCache rvk_psocache_load(RvkDevice*);
void            rvk_psocache_save(RvkDevice*, VkPipelineCache);
