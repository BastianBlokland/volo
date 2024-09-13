#pragma once
#include "core_memory.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkBuffer   RvkBuffer;
typedef struct sRvkDescMeta RvkDescMeta;
typedef struct sRvkDevice   RvkDevice;
typedef struct sRvkDescSet  RvkDescSet;

typedef struct sRvkUniformPool RvkUniformPool;

typedef u32 RvkUniformHandle; // Zero is invalid.

RvkUniformPool* rvk_uniform_pool_create(RvkDevice*);
void            rvk_uniform_pool_destroy(RvkUniformPool*);
u32             rvk_uniform_size_max(RvkUniformPool*);

bool             rvk_uniform_valid(RvkUniformHandle);
u32              rvk_uniform_size(const RvkUniformPool*, RvkUniformHandle);
RvkUniformHandle rvk_uniform_next(const RvkUniformPool*, RvkUniformHandle);

void             rvk_uniform_reset(RvkUniformPool*);
RvkUniformHandle rvk_uniform_upload(RvkUniformPool*, Mem data);
void             rvk_uniform_upload_next(RvkUniformPool*, RvkUniformHandle head, Mem data);

void rvk_uniform_attach(RvkUniformPool*, RvkUniformHandle, RvkDescSet, const u32 binding);

/**
 * Dynamic binding is a fast-path where we can allocate persistent descriptor-sets (that only
 * contain a single 'UniformBufferDynamic') for uniform data.
 * This avoids needing many temporary descriptor sets to bind small pieces of data.
 * Pre-condition: Given set needs to only require a single 'UniformBufferDynamic' at binding 0.
 */
void rvk_uniform_dynamic_bind(
    RvkUniformPool*, RvkUniformHandle, VkCommandBuffer, VkPipelineLayout, u32 set);
