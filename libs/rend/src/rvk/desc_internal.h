#pragma once
#include "vulkan_internal.h"

#define rvk_desc_bindings_max 8

typedef struct sRvkDescPool  RvkDescPool;
typedef struct sRvkDescChunk RvkDescChunk;

typedef enum {
  RvkDescKind_None,
  RvkDescKind_CombinedImageSampler,
  RvkDescKind_UniformBuffer,
  RvkDescKind_UniformBufferDynamic,
  RvkDescKind_StorageBuffer,
} RvkDescKind;

typedef struct {
  RvkDescKind bindings[rvk_desc_bindings_max];
} RvkDescMeta;

typedef struct {
  RvkDescChunk* chunk;
  usize         idx;
} RvkDescSet;

#define rvk_desc_valid(_SET_) ((_SET_).chunk != null)

RvkDescPool* rvk_desc_pool_create(VkDevice);
void         rvk_desc_pool_destroy(RvkDescPool*);

VkDescriptorSetLayout rvk_desc_vklayout(RvkDescPool*, const RvkDescMeta*);
RvkDescSet            rvk_desc_alloc(RvkDescPool*, const RvkDescMeta*);
void                  rvk_desc_free(RvkDescSet);

VkDescriptorSet       rvk_desc_set_vkset(RvkDescSet);
VkDescriptorSetLayout rvk_desc_set_vklayout(RvkDescSet);
RvkDescKind           rvk_desc_set_kind(RvkDescSet, u32 binding);
