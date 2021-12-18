#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_thread.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "desc_internal.h"
#include "image_internal.h"
#include "mem_internal.h"
#include "sampler_internal.h"

#define VOLO_RVK_DESC_LOGGING

#define rvk_desc_sets_per_chunk 8

typedef struct {
  u32                   metaHash;
  RvkDescMeta           meta;
  VkDescriptorSetLayout vkLayout;
} RvkDescLayout;

struct sRvkDescChunk {
  RvkDescPool*     pool;
  RvkDescChunk*    next;
  VkDescriptorPool vkPool;
  VkDescriptorSet  vkSets[rvk_desc_sets_per_chunk];
  u8               freeSets[bits_to_bytes(rvk_desc_sets_per_chunk) + 1];
  u32              metaHash;
};

struct sRvkDescPool {
  VkDevice              vkDev;
  VkAllocationCallbacks vkAlloc;
  ThreadMutex           layoutLock;
  DynArray              layouts; // RvkDescLayout[], kept sorted on the metaHash.
  ThreadMutex           chunkLock;
  RvkDescChunk*         chunkHead;
  RvkDescChunk*         chunkTail;
};

static u32 rvk_desc_meta_hash(const RvkDescMeta* meta) {
  return bits_hash_32(array_mem(meta->bindings));
}

static i8 rvk_desc_compare_layout(const void* a, const void* b) {
  return compare_u32(field_ptr(a, RvkDescLayout, metaHash), field_ptr(b, RvkDescLayout, metaHash));
}

static BitSet rvk_desc_chunk_mask(const RvkDescChunk* chunk) {
  return bitset_from_array(chunk->freeSets);
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
  case RvkDescKind_Count:
    break;
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

static VkDescriptorPool rvk_desc_vkpool_create(RvkDescPool* pool, const RvkDescMeta* meta) {
  VkDescriptorPoolSize sizes[rvk_desc_bindings_max];
  u32                  sizeCount = 0;

  for (u32 id = 0; id != rvk_desc_bindings_max; ++id) {
    if (meta->bindings[id]) {
      const VkDescriptorType type = rvk_desc_vktype(meta->bindings[id]);
      for (u32 sizeIdx = 0; sizeIdx != sizeCount; ++sizeIdx) {
        if (sizes[sizeIdx].type == type) {
          sizes[sizeIdx].descriptorCount += rvk_desc_sets_per_chunk;
          goto NextBinding;
        }
      }
      sizes[sizeCount++] = (VkDescriptorPoolSize){type, .descriptorCount = rvk_desc_sets_per_chunk};
    }
  NextBinding:
    continue;
  }

  if (!sizeCount) {
    // NOTE: Vulkan spec does not allow for empty descriptor pools, however supporting empty
    // bindings simplifies the api quite a bit. Needs investigation into alternatives.
    sizes[sizeCount++] =
        (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1};
  }

  const VkDescriptorPoolCreateInfo poolInfo = {
      .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .poolSizeCount = sizeCount,
      .pPoolSizes    = sizes,
      .maxSets       = rvk_desc_sets_per_chunk,
  };
  VkDescriptorPool result;
  rvk_call(vkCreateDescriptorPool, pool->vkDev, &poolInfo, &pool->vkAlloc, &result);
  return result;
}

static RvkDescChunk* rvk_desc_chunk_create(RvkDescPool* pool, const RvkDescMeta* meta) {
  RvkDescChunk* chunk = alloc_alloc_t(g_alloc_heap, RvkDescChunk);
  *chunk              = (RvkDescChunk){
      .pool     = pool,
      .vkPool   = rvk_desc_vkpool_create(pool, meta),
      .metaHash = rvk_desc_meta_hash(meta),
  };

  const BitSet                freeMask = rvk_desc_chunk_mask(chunk);
  const VkDescriptorSetLayout vkLayout = rvk_desc_vklayout(pool, meta);

  VkDescriptorSetLayout layouts[rvk_desc_sets_per_chunk];
  for (usize i = 0; i != rvk_desc_sets_per_chunk; ++i) {
    layouts[i] = vkLayout;   // Use the same layout for all sets in the chunk.
    bitset_set(freeMask, i); // Mark the set as available.
  }

  // Preallocate all the descriptor sets.
  const VkDescriptorSetAllocateInfo allocInfo = {
      .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool     = chunk->vkPool,
      .descriptorSetCount = rvk_desc_sets_per_chunk,
      .pSetLayouts        = layouts,
  };
  rvk_call(vkAllocateDescriptorSets, pool->vkDev, &allocInfo, chunk->vkSets);

#ifdef VOLO_RVK_DESC_LOGGING
  log_d(
      "Vulkan descriptor chunk created",
      log_param("bindings", fmt_int(rvk_desc_binding_count(meta))),
      log_param("sets", fmt_int(rvk_desc_sets_per_chunk)),
      log_param("meta-hash", fmt_int(chunk->metaHash)));
#endif
  return chunk;
}

static void rvk_desc_chunk_destroy(RvkDescChunk* chunk) {
  diag_assert_msg(
      bitset_count(rvk_desc_chunk_mask(chunk)) == rvk_desc_sets_per_chunk,
      "Not all descriptor sets have been freed");

  vkDestroyDescriptorPool(chunk->pool->vkDev, chunk->vkPool, &chunk->pool->vkAlloc);
  alloc_free_t(g_alloc_heap, chunk);

#ifdef VOLO_RVK_DESC_LOGGING
  log_d("Vulkan descriptor chunk destroyed", log_param("meta-hash", fmt_int(chunk->metaHash)));
#endif
}

static RvkDescSet rvk_desc_chunk_alloc(RvkDescChunk* chunk) {
  const BitSet freeMask = rvk_desc_chunk_mask(chunk);
  const usize  next     = bitset_next(freeMask, 0);
  if (sentinel_check(next)) {
    return (RvkDescSet){0}; // No set available.
  }
  bitset_clear(freeMask, next); // Mark the set as used.
  return (RvkDescSet){.chunk = chunk, .idx = next};
}

static void rvk_desc_chunk_free(RvkDescChunk* chunk, const RvkDescSet set) {
  diag_assert(set.chunk == chunk);

  const BitSet freeMask = rvk_desc_chunk_mask(chunk);
  diag_assert(!bitset_test(freeMask, set.idx)); // Check if its not freed before.
  bitset_set(freeMask, set.idx);                // Mark the set as available.
}

RvkDescPool* rvk_desc_pool_create(const VkDevice vkDev) {
  RvkDescPool* pool = alloc_alloc_t(g_alloc_heap, RvkDescPool);
  *pool             = (RvkDescPool){
      .vkDev      = vkDev,
      .vkAlloc    = rvk_mem_allocator(g_alloc_heap),
      .layoutLock = thread_mutex_create(g_alloc_heap),
      .layouts    = dynarray_create_t(g_alloc_heap, RvkDescLayout, 64),
      .chunkLock  = thread_mutex_create(g_alloc_heap),
  };
  return pool;
}

void rvk_desc_pool_destroy(RvkDescPool* pool) {

  // Destroy all chunks.
  thread_mutex_destroy(pool->chunkLock);
  for (RvkDescChunk* chunk = pool->chunkHead; chunk;) {
    RvkDescChunk* toDestroy = chunk;
    chunk                   = chunk->next;
    rvk_desc_chunk_destroy(toDestroy);
  }

  // Destroy all layouts.
  thread_mutex_destroy(pool->layoutLock);
  dynarray_for_t(&pool->layouts, RvkDescLayout, layout) {
    vkDestroyDescriptorSetLayout(pool->vkDev, layout->vkLayout, &pool->vkAlloc);
  }
  dynarray_destroy(&pool->layouts);

  alloc_free_t(g_alloc_heap, pool);
}

VkDescriptorSetLayout rvk_desc_vklayout(RvkDescPool* pool, const RvkDescMeta* meta) {
  const u32 hash = rvk_desc_meta_hash(meta);

  thread_mutex_lock(pool->layoutLock);

  // Find an existing layout that matches the given descriptor meta or create a new one.
  RvkDescLayout* tgt    = mem_struct(RvkDescLayout, .metaHash = hash).ptr;
  RvkDescLayout* layout = dynarray_search_binary(&pool->layouts, rvk_desc_compare_layout, tgt);

  if (!layout) {
    layout  = dynarray_insert_sorted_t(&pool->layouts, RvkDescLayout, rvk_desc_compare_layout, tgt);
    *layout = (RvkDescLayout){
        .metaHash = hash,
        .meta     = *meta,
        .vkLayout = rvk_desc_vklayout_create(pool, meta),
    };

#ifdef VOLO_RVK_DESC_LOGGING
    log_d(
        "Vulkan descriptor layout created",
        log_param("bindings", fmt_int(rvk_desc_binding_count(meta))),
        log_param("meta-hash", fmt_int(hash)));
#endif
  }

  const VkDescriptorSetLayout result = layout->vkLayout;
  thread_mutex_unlock(pool->layoutLock);
  return result;
}

RvkDescSet rvk_desc_alloc(RvkDescPool* pool, const RvkDescMeta* meta) {
  RvkDescSet result = {0};
  const u32  hash   = rvk_desc_meta_hash(meta);
  thread_mutex_lock(pool->chunkLock);

  // Attempt to allocate from an existing chunk.
  for (RvkDescChunk* chunk = pool->chunkHead; chunk; chunk = chunk->next) {
    if (chunk->metaHash == hash) {
      result = rvk_desc_chunk_alloc(chunk);
      if (rvk_desc_valid(result)) {
        goto Done;
      }
    }
  }
  // No existing chunk has a set available; create a new chunk.
  RvkDescChunk* chunk = rvk_desc_chunk_create(pool, meta);
  result              = rvk_desc_chunk_alloc(chunk);
  if (!pool->chunkHead) {
    pool->chunkHead = chunk;
  }
  if (pool->chunkTail) {
    pool->chunkTail->next = chunk;
  }
  pool->chunkTail = chunk;

Done:
  diag_assert(rvk_desc_valid(result));
  thread_mutex_unlock(pool->chunkLock);
  return result;
}

void rvk_desc_free(RvkDescSet set) {
  diag_assert(rvk_desc_valid(set));

  thread_mutex_lock(set.chunk->pool->chunkLock);
  rvk_desc_chunk_free(set.chunk, set);
  thread_mutex_unlock(set.chunk->pool->chunkLock);
}

String rvk_desc_kind_str(const RvkDescKind kind) {
  static const String names[] = {
      string_static("None"),
      string_static("CombinedImageSampler"),
      string_static("UniformBuffer"),
      string_static("UniformBufferDynamic"),
      string_static("StorageBuffer"),
  };
  ASSERT(array_elems(names) == RvkDescKind_Count, "Incorrect number of names");
  return names[kind];
}

VkDescriptorSet rvk_desc_set_vkset(const RvkDescSet set) {
  diag_assert(rvk_desc_valid(set));
  return set.chunk->vkSets[set.idx];
}

VkDescriptorSetLayout rvk_desc_set_vklayout(const RvkDescSet set) {
  diag_assert(rvk_desc_valid(set));

  thread_mutex_lock(set.chunk->pool->layoutLock);

  const RvkDescLayout* layout = dynarray_search_binary(
      &set.chunk->pool->layouts,
      rvk_desc_compare_layout,
      mem_struct(RvkDescLayout, .metaHash = set.chunk->metaHash).ptr);

  const VkDescriptorSetLayout result = layout->vkLayout;
  thread_mutex_unlock(set.chunk->pool->layoutLock);
  return result;
}

RvkDescKind rvk_desc_set_kind(const RvkDescSet set, const u32 binding) {
  diag_assert(rvk_desc_valid(set));

  thread_mutex_lock(set.chunk->pool->layoutLock);

  const RvkDescLayout* layout = dynarray_search_binary(
      &set.chunk->pool->layouts,
      rvk_desc_compare_layout,
      mem_struct(RvkDescLayout, .metaHash = set.chunk->metaHash).ptr);

  const RvkDescKind result = layout->meta.bindings[binding];
  thread_mutex_unlock(set.chunk->pool->layoutLock);
  return result;
}

void rvk_desc_set_attach_buffer(const RvkDescSet set, const u32 binding, const RvkBuffer* buffer) {
  const RvkDescKind kind = rvk_desc_set_kind(set, binding);
  diag_assert(kind);

  const VkDescriptorBufferInfo bufferInfo = {
      .buffer = buffer->vkBuffer,
      .offset = 0,
      .range  = buffer->mem.size,
  };
  const VkWriteDescriptorSet descriptorWrite = {
      .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet          = rvk_desc_set_vkset(set),
      .dstBinding      = binding,
      .dstArrayElement = 0,
      .descriptorType  = rvk_desc_vktype(kind),
      .descriptorCount = 1,
      .pBufferInfo     = &bufferInfo,
  };
  vkUpdateDescriptorSets(set.chunk->pool->vkDev, 1, &descriptorWrite, 0, null);
}

void rvk_desc_set_attach_sampler(
    const RvkDescSet set, const u32 binding, const RvkImage* image, const RvkSampler* sampler) {

  const RvkDescKind kind = rvk_desc_set_kind(set, binding);
  diag_assert(kind == RvkDescKind_CombinedImageSampler);

  VkDescriptorImageInfo imgInfo = {
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .imageView   = image->vkImageView,
      .sampler     = sampler->vkSampler,
  };
  VkWriteDescriptorSet descriptorWrite = {
      .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet          = rvk_desc_set_vkset(set),
      .dstBinding      = binding,
      .dstArrayElement = 0,
      .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .pImageInfo      = &imgInfo,
  };
  vkUpdateDescriptorSets(set.chunk->pool->vkDev, 1, &descriptorWrite, 0, null);
}
