#pragma once
#include "forward_internal.h"

#include "vulkan_api.h"

VkPipelineCache rvk_pcache_load(RvkDevice*);
void            rvk_pcache_save(RvkDevice*, VkPipelineCache);
