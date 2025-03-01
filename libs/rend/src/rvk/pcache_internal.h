#pragma once
#include "vulkan_api.h"

#include "forward_internal.h"

VkPipelineCache rvk_pcache_load(RvkDevice*);
void            rvk_pcache_save(RvkDevice*, VkPipelineCache);
