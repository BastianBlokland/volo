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
RvkDescMeta     rvk_uniform_meta(RvkUniformPool*);

typedef struct {
  u32 chunkIdx, offset;
} RvkUniformHandle;

void             rvk_uniform_reset(RvkUniformPool*);
RvkUniformHandle rvk_uniform_upload(RvkUniformPool*, Mem data);
const RvkBuffer* rvk_uniform_buffer(RvkUniformPool*, RvkUniformHandle);

void rvk_uniform_bind(
    RvkUniformPool*, RvkUniformHandle, VkCommandBuffer, VkPipelineLayout, u32 set);
