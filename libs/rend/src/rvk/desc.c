#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_thread.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "desc_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "lib_internal.h"
#include "sampler_internal.h"

// #define VOLO_RVK_DESC_LOGGING

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
  RvkDevice*    dev;
  ThreadMutex   layoutLock;
  DynArray      layouts; // RvkDescLayout[], kept sorted on the metaHash.
  ThreadMutex   chunkLock;
  RvkDescChunk* chunkHead;
  RvkDescChunk* chunkTail;
  bool          warnedForUnableToClear;
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
  case RvkDescKind_CombinedImageSampler2D:
  case RvkDescKind_CombinedImageSamplerCube:
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
  rvk_call_checked(
      pool->dev,
      createDescriptorSetLayout,
      pool->dev->vkDev,
      &layoutInfo,
      &pool->dev->vkAlloc,
      &result);
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
    /**
     * NOTE: Vulkan spec does not allow for empty descriptor pools, however supporting empty
     * bindings simplifies the api quite a bit. Needs investigation into alternatives.
     */
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
  rvk_call_checked(
      pool->dev, createDescriptorPool, pool->dev->vkDev, &poolInfo, &pool->dev->vkAlloc, &result);
  return result;
}

static RvkDescChunk* rvk_desc_chunk_create(RvkDescPool* pool, const RvkDescMeta* meta) {
  RvkDescChunk* chunk = alloc_alloc_t(g_allocHeap, RvkDescChunk);

  *chunk = (RvkDescChunk){
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
  rvk_call_checked(pool->dev, allocateDescriptorSets, pool->dev->vkDev, &allocInfo, chunk->vkSets);

#if defined(VOLO_RVK_DESC_LOGGING)
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

  RvkDevice* dev = chunk->pool->dev;
  rvk_call(dev, destroyDescriptorPool, dev->vkDev, chunk->vkPool, &dev->vkAlloc);
  alloc_free_t(g_allocHeap, chunk);

#if defined(VOLO_RVK_DESC_LOGGING)
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

RvkDescPool* rvk_desc_pool_create(RvkDevice* dev) {
  RvkDescPool* pool = alloc_alloc_t(g_allocHeap, RvkDescPool);

  *pool = (RvkDescPool){
      .dev        = dev,
      .layoutLock = thread_mutex_create(g_allocHeap),
      .layouts    = dynarray_create_t(g_allocHeap, RvkDescLayout, 64),
      .chunkLock  = thread_mutex_create(g_allocHeap),
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
  RvkDevice* dev = pool->dev;
  thread_mutex_destroy(pool->layoutLock);
  dynarray_for_t(&pool->layouts, RvkDescLayout, layout) {
    rvk_call(dev, destroyDescriptorSetLayout, dev->vkDev, layout->vkLayout, &dev->vkAlloc);
  }
  dynarray_destroy(&pool->layouts);

  alloc_free_t(g_allocHeap, pool);
}

u16 rvk_desc_pool_sets_occupied(const RvkDescPool* pool) {
  thread_mutex_lock(pool->chunkLock);
  u16 occupied = 0;
  for (RvkDescChunk* chunk = pool->chunkHead; chunk; chunk = chunk->next) {
    const u16 numFree = (u16)bitset_count(rvk_desc_chunk_mask(chunk));
    occupied += rvk_desc_sets_per_chunk - numFree;
  }
  thread_mutex_unlock(pool->chunkLock);
  return occupied;
}

u16 rvk_desc_pool_sets_reserved(const RvkDescPool* pool) {
  thread_mutex_lock(pool->chunkLock);
  u16 reserved = 0;
  for (RvkDescChunk* chunk = pool->chunkHead; chunk; chunk = chunk->next) {
    reserved += rvk_desc_sets_per_chunk;
  }
  thread_mutex_unlock(pool->chunkLock);
  return reserved;
}

u16 rvk_desc_pool_layouts(const RvkDescPool* pool) {
  thread_mutex_lock(pool->layoutLock);
  const u16 layouts = (u16)pool->layouts.size;
  thread_mutex_unlock(pool->layoutLock);
  return layouts;
}

bool rvk_desc_empty(const RvkDescMeta* meta) {
  for (u32 i = 0; i != rvk_desc_bindings_max; ++i) {
    if (meta->bindings[i]) {
      return false;
    }
  }
  return true;
}

VkDescriptorSetLayout rvk_desc_vklayout(RvkDescPool* pool, const RvkDescMeta* meta) {
  const u32 hash = rvk_desc_meta_hash(meta);

  thread_mutex_lock(pool->layoutLock);

  // Find an existing layout that matches the given descriptor meta or create a new one.
  RvkDescLayout* tgt = mem_struct(RvkDescLayout, .metaHash = hash).ptr;
  RvkDescLayout* layout =
      dynarray_find_or_insert_sorted(&pool->layouts, rvk_desc_compare_layout, tgt);

  if (layout->metaHash != hash) {
    *layout = (RvkDescLayout){
        .metaHash = hash,
        .meta     = *meta,
        .vkLayout = rvk_desc_vklayout_create(pool, meta),
    };

#if defined(VOLO_RVK_DESC_LOGGING)
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

void rvk_desc_free(RvkDescSet set) { rvk_desc_free_batch(&set, 1); }

void rvk_desc_free_batch(const RvkDescSet sets[], const usize count) {
  if (!count) {
    return;
  }

  rvk_desc_set_clear_batch(sets, count);

  RvkDescPool* pool = sets[0].chunk->pool;
  thread_mutex_lock(pool->chunkLock);
  for (u32 i = 0; i != count; ++i) {
    diag_assert(sets[i].chunk->pool == pool);
    rvk_desc_chunk_free(sets[i].chunk, sets[i]);
  }
  thread_mutex_unlock(pool->chunkLock);
}

String rvk_desc_kind_str(const RvkDescKind kind) {
  static const String g_names[] = {
      string_static("None"),
      string_static("CombinedImageSampler2D"),
      string_static("CombinedImageSamplerCube"),
      string_static("UniformBuffer"),
      string_static("UniformBufferDynamic"),
      string_static("StorageBuffer"),
  };
  ASSERT(array_elems(g_names) == RvkDescKind_Count, "Incorrect number of names");
  return g_names[kind];
}

bool rvk_desc_kind_is_buffer(const RvkDescKind kind) {
  switch (kind) {
  case RvkDescKind_UniformBuffer:
  case RvkDescKind_UniformBufferDynamic:
  case RvkDescKind_StorageBuffer:
    return true;
  default:
    return false;
  }
}

bool rvk_desc_kind_is_sampler(const RvkDescKind kind) {
  switch (kind) {
  case RvkDescKind_CombinedImageSampler2D:
  case RvkDescKind_CombinedImageSamplerCube:
    return true;
  default:
    return false;
  }
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

RvkDescMeta rvk_desc_set_meta(const RvkDescSet set) {
  diag_assert(rvk_desc_valid(set));

  thread_mutex_lock(set.chunk->pool->layoutLock);

  const RvkDescLayout* layout = dynarray_search_binary(
      &set.chunk->pool->layouts,
      rvk_desc_compare_layout,
      mem_struct(RvkDescLayout, .metaHash = set.chunk->metaHash).ptr);

  const RvkDescMeta result = layout->meta;
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

void rvk_desc_set_name(const RvkDescSet set, const String dbgName) {
  RvkDevice* dev = set.chunk->pool->dev;
  if (!(dev->lib->flags & RvkLibFlags_Debug)) {
    return;
  }
  VkDescriptorSet vkSet = set.chunk->vkSets[set.idx];
  (void)vkSet;
  (void)dbgName;

  rvk_debug_name_fmt(
      dev, VK_OBJECT_TYPE_DESCRIPTOR_SET, vkSet, "descriptor_set_{}", fmt_text(dbgName));
}

void rvk_desc_set_clear(const RvkDescSet set) { rvk_desc_set_clear_batch(&set, 1); }

void rvk_desc_set_clear_batch(const RvkDescSet sets[], const usize count) {
  if (!count) {
    return;
  }
  RvkDescPool* pool = sets[0].chunk->pool;
  RvkDevice*   dev  = pool->dev;
  if (!(pool->dev->flags & RvkDeviceFlags_SupportNullDescriptor)) {
    /**
     * If the device does not support a null-descriptor we have no way to clear it as there's no api
     * for a descriptor-set to go back to the initial undefined state.
     *
     * One option would be to set all bindings to dummy (but valid) images and buffers. Another
     * option is to just ignore this as in practice if you don't access the invalid bindings then
     * drivers are fine with it, but the validator layers will raise it as an issue if you destroy a
     * resource that is still referenced in a descriptor-set.
     */
    if (!pool->warnedForUnableToClear) {
      log_w("Unable to clear descriptor set");
      pool->warnedForUnableToClear = true;
    }
    return;
  }

  VkDescriptorImageInfo nullImage = {
      .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .imageView   = null,
      .sampler     = rvk_sampler_get(dev->samplerPool, (RvkSamplerSpec){0}),
  };
  VkDescriptorBufferInfo nullBuffer = {
      .buffer = null,
      .offset = 0,
      .range  = VK_WHOLE_SIZE,
  };

  const u32             writesMax = (u32)count * rvk_desc_bindings_max;
  VkWriteDescriptorSet* writes    = alloc_array_t(g_allocScratch, VkWriteDescriptorSet, writesMax);
  u32                   writesCount = 0;

  for (usize i = 0; i != count; ++i) {
    diag_assert(sets[i].chunk->pool == pool);

    const RvkDescMeta meta = rvk_desc_set_meta(sets[i]);
    for (u32 binding = 0; binding != array_elems(meta.bindings); ++binding) {
      if (meta.bindings[binding] == RvkDescKind_None) {
        continue; // Unused binding.
      }
      VkWriteDescriptorSet* write = &writes[writesCount++];

      *write = (VkWriteDescriptorSet){
          .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet          = rvk_desc_set_vkset(sets[i]),
          .dstBinding      = binding,
          .dstArrayElement = 0,
          .descriptorType  = rvk_desc_vktype(meta.bindings[binding]),
          .descriptorCount = 1,
      };

      switch (meta.bindings[binding]) {
      case RvkDescKind_CombinedImageSampler2D:
      case RvkDescKind_CombinedImageSamplerCube:
        write->pImageInfo = &nullImage;
        continue;
      case RvkDescKind_UniformBuffer:
      case RvkDescKind_UniformBufferDynamic:
      case RvkDescKind_StorageBuffer:
        write->pBufferInfo = &nullBuffer;
        continue;
      }
      diag_crash_msg("Unsupported binding");
    }
  }

  rvk_call(dev, updateDescriptorSets, dev->vkDev, writesCount, writes, 0, null);
}

void rvk_desc_update_buffer(
    RvkDescUpdateBatch* batch,
    const RvkDescSet    set,
    const u32           binding,
    const RvkBuffer*    buffer,
    const u32           offset,
    const u32           size) {

  if (batch->count == array_elems(batch->buffer)) {
    rvk_desc_update_flush(batch);
  }
  batch->buffer[batch->count++] = (RvkDescUpdate){
      .set     = set,
      .binding = binding,
      .type    = RvkDescUpdateType_Buffer,
      .buffer  = {.buffer = buffer, .offset = offset, .size = size},
  };
}

void rvk_desc_update_sampler(
    RvkDescUpdateBatch*  batch,
    const RvkDescSet     set,
    const u32            binding,
    const RvkImage*      image,
    const RvkSamplerSpec spec) {

  if (batch->count == array_elems(batch->buffer)) {
    rvk_desc_update_flush(batch);
  }
  batch->buffer[batch->count++] = (RvkDescUpdate){
      .set     = set,
      .binding = binding,
      .type    = RvkDescUpdateType_Sampler,
      .sampler = {.image = image, .spec = spec},
  };
}

void rvk_desc_update_flush(RvkDescUpdateBatch* batch) {
  if (!batch->count) {
    return;
  }
  RvkDescPool* pool = batch->buffer[0].set.chunk->pool;
  RvkDevice*   dev  = pool->dev;

  VkDescriptorBufferInfo buffInfos[array_elems(batch->buffer)];
  u32                    buffCount = 0;

  VkDescriptorImageInfo imageInfos[array_elems(batch->buffer)];
  u32                   imageCount = 0;

  VkWriteDescriptorSet writes[array_elems(batch->buffer)];
  u32                  writesCount = 0;

  for (usize i = 0; i != batch->count; ++i) {
    const RvkDescUpdate* update = &batch->buffer[i];
    diag_assert(update->set.chunk->pool == pool);

    const RvkDescKind kind = rvk_desc_set_kind(update->set, update->binding);
    if (UNLIKELY(!kind)) {
      diag_assert_fail("Invalid descriptor binding");
      continue;
    }

    VkWriteDescriptorSet* write = &writes[writesCount];

    *write = (VkWriteDescriptorSet){
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = rvk_desc_set_vkset(update->set),
        .dstBinding      = update->binding,
        .dstArrayElement = 0,
        .descriptorType  = rvk_desc_vktype(kind),
        .descriptorCount = 1,
    };

    switch (update->type) {
    case RvkDescUpdateType_Buffer: {
      if (UNLIKELY(!rvk_desc_kind_is_buffer(kind))) {
        diag_assert_fail("Descriptor binding is not a buffer");
        continue;
      }
      const RvkBuffer* buffer = update->buffer.buffer;
      const u32        offset = update->buffer.offset;
      const u32        size   = update->buffer.size;
      diag_assert((offset + size) <= buffer->size);

      buffInfos[buffCount] = (VkDescriptorBufferInfo){
          .buffer = buffer->vkBuffer,
          .offset = offset,
          .range  = size ? size : (buffer->size - offset),
      };
      write->pBufferInfo = &buffInfos[buffCount++];
    } break;
    case RvkDescUpdateType_Sampler: {
      if (UNLIKELY(!rvk_desc_kind_is_sampler(kind))) {
        diag_assert_fail("Descriptor binding is not a sampler");
        continue;
      }
      const RvkImage* image = update->sampler.image;
      diag_assert(image->caps & RvkImageCapability_Sampled);

      const RvkSamplerSpec spec         = update->sampler.spec;
      const bool           needsCubeMap = kind == RvkDescKind_CombinedImageSamplerCube;
      if (needsCubeMap && image->type != RvkImageType_ColorSourceCube) {
        diag_assert_fail("Descriptor needs a cube-map image");
        continue;
      }
      imageInfos[imageCount] = (VkDescriptorImageInfo){
          .imageLayout = image->type == RvkImageType_DepthAttachment
                             ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                             : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .imageView   = image->vkImageView,
          .sampler     = rvk_sampler_get(pool->dev->samplerPool, spec),
      };
      write->pImageInfo = &imageInfos[imageCount++];
    } break;
    }
    ++writesCount; // Write locked in.
  }

  batch->count = 0;
  rvk_call(dev, updateDescriptorSets, dev->vkDev, writesCount, writes, 0, null);
}

void rvk_desc_group_bind(RvkDescGroup* group, const u32 setIndex, const RvkDescSet set) {
  group->dirtySets[setIndex] = set;
}

void rvk_desc_group_flush(
    RvkDescGroup* group, const VkCommandBuffer vkCmdBuf, const VkPipelineLayout vkPipelineLayout) {
  RvkDescPool* pool = null;
  for (u32 setIndex = 0; setIndex != array_elems(group->dirtySets); ++setIndex) {
    RvkDescSet dirtySet = group->dirtySets[setIndex];
    if (!rvk_desc_valid(dirtySet)) {
      continue;
    }
    diag_assert(!pool || pool == dirtySet.chunk->pool);
    pool = dirtySet.chunk->pool;

    const VkDescriptorSet vkDescSet = rvk_desc_set_vkset(dirtySet);
    rvk_call(
        pool->dev,
        cmdBindDescriptorSets,
        vkCmdBuf,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        vkPipelineLayout,
        setIndex,
        1,
        &vkDescSet,
        0,
        null);
  }
  mem_set(array_mem(group->dirtySets), 0);
}
