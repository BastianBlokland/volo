#pragma once
#include "core_memory.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkBuffer   RvkBuffer;
typedef struct sRvkDescMeta RvkDescMeta;
typedef struct sRvkDevice   RvkDevice;

typedef struct sRvkUniformPool RvkUniformPool;

RvkUniformPool* rvk_uniform_pool_create(RvkDevice*);
void            rvk_uniform_pool_destroy(RvkUniformPool*);
u32             rvk_uniform_size_max(RvkUniformPool*);

typedef struct sRvkUniformHandle {
  u32 chunkIdx, offset;
} RvkUniformHandle;

void             rvk_uniform_reset(RvkUniformPool*);
RvkUniformHandle rvk_uniform_upload(RvkUniformPool*, Mem data);
const RvkBuffer* rvk_uniform_buffer(RvkUniformPool*, RvkUniformHandle);

/**
 * Dynamic binding is a fast-path where we can allocate persistent descriptor-sets (that only
 * contain a single 'UniformBufferDynamic') for uniform data.
 * This avoids needing many temporary descriptor sets to bind small pieces of data.
 * Pre-condition: Given set needs to only require a single 'UniformBufferDynamic' at binding 0.
 */
void rvk_uniform_dynamic_bind(
    RvkUniformPool*, RvkUniformHandle, VkCommandBuffer, VkPipelineLayout, u32 set);
