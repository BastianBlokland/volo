#pragma once
#include "core_memory.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef struct sRvkUniformPool RvkUniformPool;

RvkUniformPool*       rvk_uniform_pool_create(RvkDevice*);
void                  rvk_uniform_pool_destroy(RvkUniformPool*);
u32                   rvk_uniform_size_max(RvkUniformPool*);
VkDescriptorSetLayout rvk_uniform_vkdesclayout(RvkUniformPool*);

void rvk_uniform_reset(RvkUniformPool*);
void rvk_uniform_bind(RvkUniformPool*, Mem data, VkCommandBuffer, VkPipelineLayout, u32 set);
