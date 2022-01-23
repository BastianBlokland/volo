#pragma once
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkBuffer  RvkBuffer;
typedef struct sRvkImage   RvkImage;
typedef struct sRvkSampler RvkSampler;

#define rvk_desc_bindings_max 8

typedef struct sRvkDescPool  RvkDescPool;
typedef struct sRvkDescChunk RvkDescChunk;

typedef enum {
  RvkDescKind_None,
  RvkDescKind_CombinedImageSampler,
  RvkDescKind_UniformBuffer,
  RvkDescKind_UniformBufferDynamic,
  RvkDescKind_StorageBuffer,

  RvkDescKind_Count,
} RvkDescKind;

typedef struct {
  u8 bindings[rvk_desc_bindings_max]; // RvkDescKind[]
} RvkDescMeta;

typedef struct {
  RvkDescChunk* chunk;
  usize         idx;
} RvkDescSet;

#define rvk_desc_valid(_SET_) ((_SET_).chunk != null)

RvkDescPool* rvk_desc_pool_create(VkDevice);
void         rvk_desc_pool_destroy(RvkDescPool*);
u32          rvk_desc_pool_sets_occupied(const RvkDescPool*);
u32          rvk_desc_pool_sets_reserved(const RvkDescPool*);
u32          rvk_desc_pool_layouts(const RvkDescPool*);

VkDescriptorSetLayout rvk_desc_vklayout(RvkDescPool*, const RvkDescMeta*);
RvkDescSet            rvk_desc_alloc(RvkDescPool*, const RvkDescMeta*);
void                  rvk_desc_free(RvkDescSet);
String                rvk_desc_kind_str(RvkDescKind);

VkDescriptorSet       rvk_desc_set_vkset(RvkDescSet);
VkDescriptorSetLayout rvk_desc_set_vklayout(RvkDescSet);
RvkDescKind           rvk_desc_set_kind(RvkDescSet, u32 binding);

void rvk_desc_set_attach_buffer(RvkDescSet, u32 binding, const RvkBuffer*, u32 size);
void rvk_desc_set_attach_sampler(RvkDescSet, u32 binding, const RvkImage*, const RvkSampler*);
