#pragma once
#include "forward_internal.h"
#include "vulkan_internal.h"

VkPipelineCache rvk_pcache_load(RvkDevice*);
void            rvk_pcache_save(RvkDevice*, VkPipelineCache);
