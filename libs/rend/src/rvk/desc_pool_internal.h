#pragma once
#include "vulkan_internal.h"

#define rvk_desc_bindings_max 16

typedef struct sRvkDescPool RvkDescPool;

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

RvkDescPool* rvk_desc_pool_create(VkDevice);
void         rvk_desc_pool_destroy(RvkDescPool*);

VkDescriptorSetLayout rvk_desc_vklayout(RvkDescPool*, const RvkDescMeta*);
