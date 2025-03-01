#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "debug_internal.h"
#include "desc_internal.h"
#include "device_internal.h"
#include "lib_internal.h"
#include "uniform_internal.h"

/**
 * Maximum amount of data that we can bind to a single uniform.
 * NOTE: might be lower if the 'maxUniformBufferRange' device limit is lower.
 */
#define rvk_uniform_desired_size_max (256 * usize_kibibyte)

/**
 * Size of the backing buffers to allocate.
 */
#define rvk_uniform_buffer_size (16 * usize_mebibyte)

typedef struct {
  RvkBuffer  buffer;
  u32        offset, offsetFlushed;
  RvkDescSet dynamicSet; // Optional descriptor set for dynamic binding.
} RvkUniformChunk;

typedef struct {
  u32              chunkIdx, offset, size;
  RvkUniformHandle next;
} RvkUniformEntry;

struct sRvkUniformPool {
  RvkDevice* dev;
  u32        alignMin, dataSizeMax;
  DynArray   chunks;  // RvkUniformChunk[]
  DynArray   entries; // RvkUniformEntry[]
};

static const RvkUniformEntry* rvk_uniform_entry(const RvkUniformPool* u, const RvkUniformHandle h) {
  diag_assert_msg(h, "Invalid uniform handle");
  return dynarray_at_t(&u->entries, h - 1, RvkUniformEntry);
}

static RvkUniformEntry* rvk_uniform_entry_mut(RvkUniformPool* u, const RvkUniformHandle h) {
  diag_assert_msg(h, "Invalid uniform handle");
  return dynarray_at_t(&u->entries, h - 1, RvkUniformEntry);
}

static RvkUniformHandle rvk_uniform_entry_push(
    RvkUniformPool* uni, const u32 chunkIndex, const u32 offset, const u32 size) {
  *dynarray_push_t(&uni->entries, RvkUniformEntry) = (RvkUniformEntry){
      .chunkIdx = chunkIndex,
      .offset   = offset,
      .size     = size,
  };
  return (RvkUniformHandle)uni->entries.size;
}

static RvkUniformChunk* rvk_uniform_chunk(RvkUniformPool* uni, const u32 chunkIdx) {
  return dynarray_at_t(&uni->chunks, chunkIdx, RvkUniformChunk);
}

RvkUniformPool* rvk_uniform_pool_create(RvkDevice* dev) {
  RvkUniformPool* pool = alloc_alloc_t(g_allocHeap, RvkUniformPool);

  const u32 alignMin = (u32)dev->vkProperties.limits.minUniformBufferOffsetAlignment;
  const u32 dataSizeMax =
      (u32)math_min(dev->vkProperties.limits.maxUniformBufferRange, rvk_uniform_desired_size_max);

  *pool = (RvkUniformPool){
      .dev         = dev,
      .alignMin    = alignMin,
      .dataSizeMax = dataSizeMax,
      .chunks      = dynarray_create_t(g_allocHeap, RvkUniformChunk, 16),
      .entries     = dynarray_create_t(g_allocHeap, RvkUniformEntry, 128),
  };

  return pool;
}

void rvk_uniform_pool_destroy(RvkUniformPool* uni) {
  dynarray_for_t(&uni->chunks, RvkUniformChunk, chunk) {
    rvk_buffer_destroy(&chunk->buffer, uni->dev);
    if (rvk_desc_valid(chunk->dynamicSet)) {
      rvk_desc_free(chunk->dynamicSet);
    }
  }
  dynarray_destroy(&uni->chunks);
  dynarray_destroy(&uni->entries);
  alloc_free_t(g_allocHeap, uni);
}

u32 rvk_uniform_size_max(RvkUniformPool* uni) { return uni->dataSizeMax; }

bool rvk_uniform_valid(const RvkUniformHandle handle) { return handle != 0; }

u32 rvk_uniform_size(const RvkUniformPool* uni, const RvkUniformHandle handle) {
  return rvk_uniform_entry(uni, handle)->size;
}

RvkUniformHandle rvk_uniform_next(const RvkUniformPool* uni, const RvkUniformHandle handle) {
  return rvk_uniform_entry(uni, handle)->next;
}

void rvk_uniform_flush(RvkUniformPool* uni) {
  RvkBufferFlush flushes[128];
  u32            flushCount = 0;

  dynarray_for_t(&uni->chunks, RvkUniformChunk, chunk) {
    if (chunk->offset != chunk->offsetFlushed) {
      diag_assert(flushCount != array_elems(flushes));
      flushes[flushCount++] = (RvkBufferFlush){
          .buffer = &chunk->buffer,
          .offset = chunk->offsetFlushed,
          .size   = chunk->offset,
      };
    }
    chunk->offsetFlushed = chunk->offset;
  }

  rvk_buffer_flush_batch(flushes, flushCount);
}

void rvk_uniform_reset(RvkUniformPool* uni) {
  dynarray_for_t(&uni->chunks, RvkUniformChunk, chunk) {
    diag_assert_msg(chunk->offset == chunk->offsetFlushed, "UniformPool was not flushed");
    chunk->offset        = 0;
    chunk->offsetFlushed = 0;
  }
  dynarray_clear(&uni->entries);
}

Mem rvk_uniform_map(RvkUniformPool* uni, const RvkUniformHandle handle) {
  const RvkUniformEntry* entry = rvk_uniform_entry(uni, handle);
  RvkUniformChunk*       chunk = rvk_uniform_chunk(uni, entry->chunkIdx);
  return mem_slice(rvk_buffer_map(&chunk->buffer, entry->offset), 0, entry->size);
}

RvkUniformHandle rvk_uniform_push(RvkUniformPool* uni, const usize size) {
  diag_assert(size);

  const u32 padding    = bits_padding((u32)size, uni->alignMin);
  const u32 paddedSize = (u32)size + padding;
  diag_assert_msg(paddedSize <= uni->dataSizeMax, "Uniform data exceeds maximum");

  // Find space in an existing chunk.
  for (u32 chunkIdx = 0; chunkIdx != uni->chunks.size; ++chunkIdx) {
    RvkUniformChunk* chunk = rvk_uniform_chunk(uni, chunkIdx);
    /**
     * Check if this chunk still has enough space left.
     * NOTE: Even though there is only 'paddedSize' amount of space requested we still ensure that
     * at least up to 'dataSizeMax' is available, reason is for dynamic bindings we can tell Vulkan
     * to always bind up to 'dataSizeMax'. TODO: Investigate if there's a better way to do this.
     */
    if (chunk->buffer.mem.size - chunk->offset >= uni->dataSizeMax) {
      const u32 offset = chunk->offset;
      chunk->offset += paddedSize;
      return rvk_uniform_entry_push(uni, chunkIdx, offset, (u32)size);
    }
  }

  // If no chunk has enough space; create a new one.
  const u32        newChunkIdx = (u32)uni->chunks.size;
  RvkUniformChunk* newChunk    = dynarray_push_t(&uni->chunks, RvkUniformChunk);

  *newChunk = (RvkUniformChunk){
      .buffer = rvk_buffer_create(uni->dev, rvk_uniform_buffer_size, RvkBufferType_HostUniform),
      .offset = (u32)paddedSize,
  };

  rvk_debug_name_buffer(uni->dev, newChunk->buffer.vkBuffer, "uniform");

  log_d(
      "Vulkan uniform chunk created",
      log_param("size", fmt_size(rvk_uniform_buffer_size)),
      log_param("data-size-max", fmt_size(uni->dataSizeMax)),
      log_param("align-min", fmt_size(uni->alignMin)));

  return rvk_uniform_entry_push(uni, newChunkIdx, 0 /* offset */, (u32)size);
}

RvkUniformHandle
rvk_uniform_push_next(RvkUniformPool* uni, const RvkUniformHandle head, const usize size) {
  const RvkUniformHandle dataHandle = rvk_uniform_push(uni, size);

  RvkUniformEntry* tail = rvk_uniform_entry_mut(uni, head);
  for (; tail->next; tail = rvk_uniform_entry_mut(uni, tail->next))
    ;
  tail->next = dataHandle;

  return dataHandle;
}

void rvk_uniform_attach(
    RvkUniformPool* uni, const RvkUniformHandle handle, const RvkDescSet set, const u32 binding) {

  const RvkUniformEntry* entry  = rvk_uniform_entry(uni, handle);
  const RvkBuffer*       buffer = &rvk_uniform_chunk(uni, entry->chunkIdx)->buffer;
  rvk_desc_set_attach_buffer(set, binding, buffer, entry->offset, entry->size);
}

void rvk_uniform_dynamic_bind(
    RvkUniformPool*  uni,
    RvkUniformHandle handle,
    VkCommandBuffer  vkCmdBuf,
    VkPipelineLayout vkPipelineLayout,
    const u32        set) {

  const RvkUniformEntry* entry = rvk_uniform_entry(uni, handle);
  RvkUniformChunk*       chunk = rvk_uniform_chunk(uni, entry->chunkIdx);
  if (UNLIKELY(!rvk_desc_valid(chunk->dynamicSet))) {
    const RvkDescMeta meta = (RvkDescMeta){.bindings[0] = RvkDescKind_UniformBufferDynamic};
    chunk->dynamicSet      = rvk_desc_alloc(uni->dev->descPool, &meta);
    rvk_desc_set_attach_buffer(chunk->dynamicSet, 0, &chunk->buffer, 0, uni->dataSizeMax);
  }
  const VkDescriptorSet descSets[]       = {rvk_desc_set_vkset(chunk->dynamicSet)};
  const u32             dynamicOffsets[] = {entry->offset};
  rvk_call(
      uni->dev,
      cmdBindDescriptorSets,
      vkCmdBuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      vkPipelineLayout,
      set,
      array_elems(descSets),
      descSets,
      array_elems(dynamicOffsets),
      dynamicOffsets);
}
