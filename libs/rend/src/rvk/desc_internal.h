#pragma once
#include "forward_internal.h"
#include "vulkan_internal.h"

#define rvk_desc_bindings_max 8

typedef struct sRvkDescPool  RvkDescPool;
typedef struct sRvkDescChunk RvkDescChunk;

typedef enum eRvkDescKind {
  RvkDescKind_None,
  RvkDescKind_CombinedImageSampler2D,
  RvkDescKind_CombinedImageSamplerCube,
  RvkDescKind_UniformBuffer,
  RvkDescKind_UniformBufferDynamic,
  RvkDescKind_StorageBuffer,

  RvkDescKind_Count,
} RvkDescKind;

typedef struct sRvkDescMeta {
  u8 bindings[rvk_desc_bindings_max]; // RvkDescKind[]
} RvkDescMeta;

typedef struct sRvkDescSet {
  RvkDescChunk* chunk;
  usize         idx;
} RvkDescSet;

#define rvk_desc_valid(_SET_) ((_SET_).chunk != null)

RvkDescPool* rvk_desc_pool_create(RvkDevice*);
void         rvk_desc_pool_destroy(RvkDescPool*);
u16          rvk_desc_pool_sets_occupied(const RvkDescPool*);
u16          rvk_desc_pool_sets_reserved(const RvkDescPool*);
u16          rvk_desc_pool_layouts(const RvkDescPool*);

bool                  rvk_desc_empty(const RvkDescMeta*);
VkDescriptorSetLayout rvk_desc_vklayout(RvkDescPool*, const RvkDescMeta*);
RvkDescSet            rvk_desc_alloc(RvkDescPool*, const RvkDescMeta*);
void                  rvk_desc_free(RvkDescSet);
String                rvk_desc_kind_str(RvkDescKind);

VkDescriptorSet       rvk_desc_set_vkset(RvkDescSet);
VkDescriptorSetLayout rvk_desc_set_vklayout(RvkDescSet);
RvkDescMeta           rvk_desc_set_meta(RvkDescSet);
RvkDescKind           rvk_desc_set_kind(RvkDescSet, u32 binding);

void rvk_desc_set_attach_buffer(RvkDescSet, u32 binding, const RvkBuffer*, u32 offset, u32 size);
void rvk_desc_set_attach_sampler(RvkDescSet, u32 binding, const RvkImage*, RvkSamplerSpec);
