#pragma once
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

VkPipelineCache rvk_pcache_load(RvkDevice*);
void            rvk_pcache_save(RvkDevice*, VkPipelineCache);
