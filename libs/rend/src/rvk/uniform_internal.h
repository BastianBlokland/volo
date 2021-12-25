#pragma once
#include "core_memory.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef struct sRvkUniform RvkUniform;

RvkUniform*           rvk_uniform_create(RvkDevice*);
void                  rvk_uniform_destroy(RvkUniform*);
usize                 rvk_uniform_size_max(RvkUniform*);
VkDescriptorSetLayout rvk_uniform_vkdesclayout(RvkUniform*);

void rvk_uniform_reset(RvkUniform*);
void rvk_uniform_bind(RvkUniform*, Mem data, VkCommandBuffer, VkPipelineLayout, u32 set);
