#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_thread.h"
#include "log_logger.h"

#include "desc_pool_internal.h"
#include "mem_internal.h"

#define VOLO_RVK_DESC_LOGGING

typedef struct {
  u32                   metaHash;
  VkDescriptorSetLayout vkLayout;
} RvkDescLayout;

struct sRvkDescPool {
  VkDevice              vkDev;
  VkAllocationCallbacks vkAlloc;
  ThreadMutex           layoutLock;
  DynArray              layouts; // RvkDescLayout[], kept sorted on the metaHash.
};

static u32 rvk_desc_meta_hash(const RvkDescMeta* meta) {
  return bits_hash_32(array_mem(meta->bindings));
}

static i8 rvk_desc_compare_layout(const void* a, const void* b) {
  return compare_u32(field_ptr(a, RvkDescLayout, metaHash), field_ptr(b, RvkDescLayout, metaHash));
}

static VkDescriptorType rvk_desc_vktype(const RvkDescKind kind) {
  switch (kind) {
  case RvkDescKind_None:
    break;
  case RvkDescKind_CombinedImageSampler:
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  case RvkDescKind_UniformBuffer:
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case RvkDescKind_UniformBufferDynamic:
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  case RvkDescKind_StorageBuffer:
    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }
  diag_crash_msg("Unsupported binding kind");
}

MAYBE_UNUSED static usize rvk_desc_binding_count(const RvkDescMeta* meta) {
  usize res = 0;
  array_for_t(meta->bindings, RvkDescKind, kind) {
    if (*kind) {
      ++res;
    }
  }
  return res;
}

static VkDescriptorSetLayout rvk_desc_vklayout_create(RvkDescPool* pool, const RvkDescMeta* meta) {
  VkDescriptorSetLayoutBinding bindings[rvk_desc_bindings_max];
  u32                          bindingCount = 0;

  for (u32 id = 0; id != rvk_desc_bindings_max; ++id) {
    if (meta->bindings[id]) {
      bindings[bindingCount++] = (VkDescriptorSetLayoutBinding){
          .binding         = id,
          .descriptorType  = rvk_desc_vktype(meta->bindings[id]),
          .descriptorCount = 1,
          .stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS,
      };
    }
  }
  const VkDescriptorSetLayoutCreateInfo layoutInfo = {
      .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = bindingCount,
      .pBindings    = bindings,
  };
  VkDescriptorSetLayout result;
  rvk_call(vkCreateDescriptorSetLayout, pool->vkDev, &layoutInfo, &pool->vkAlloc, &result);
  return result;
}

RvkDescPool* rvk_desc_pool_create(const VkDevice vkDev) {
  RvkDescPool* pool = alloc_alloc_t(g_alloc_heap, RvkDescPool);
  *pool             = (RvkDescPool){
      .vkDev      = vkDev,
      .vkAlloc    = rvk_mem_allocator(g_alloc_heap),
      .layoutLock = thread_mutex_create(g_alloc_heap),
      .layouts    = dynarray_create_t(g_alloc_heap, RvkDescLayout, 64),
  };
  return pool;
}

void rvk_desc_pool_destroy(RvkDescPool* pool) {

  thread_mutex_destroy(pool->layoutLock);
  dynarray_for_t(&pool->layouts, RvkDescLayout, layout) {
    vkDestroyDescriptorSetLayout(pool->vkDev, layout->vkLayout, &pool->vkAlloc);
  }
  dynarray_destroy(&pool->layouts);

  alloc_free_t(g_alloc_heap, pool);
}

VkDescriptorSetLayout rvk_desc_layout(RvkDescPool* pool, const RvkDescMeta* meta) {
  const u32 hash = rvk_desc_meta_hash(meta);

  thread_mutex_lock(pool->layoutLock);

  /**
   * Find an existing layout that matches the given descriptor meta or create a new one.
   */
  RvkDescLayout* tgt    = mem_struct(RvkDescLayout, .metaHash = hash).ptr;
  RvkDescLayout* result = dynarray_search_binary(&pool->layouts, rvk_desc_compare_layout, tgt);

  if (!result) {
    result  = dynarray_insert_sorted_t(&pool->layouts, RvkDescLayout, rvk_desc_compare_layout, tgt);
    *result = (RvkDescLayout){
        .metaHash = hash,
        .vkLayout = rvk_desc_vklayout_create(pool, meta),
    };

#ifdef VOLO_RVK_DESC_LOGGING
    log_d(
        "Vulkan descriptor layout created",
        log_param("bindings", fmt_int(rvk_desc_binding_count(meta))),
        log_param("hash", fmt_int(hash)));
#endif
  }

  thread_mutex_unlock(pool->layoutLock);
  return result->vkLayout;
}
