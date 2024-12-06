#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_thread.h"

#include "mem_internal.h"

/**
 * Device memory pool.
 *
 * Uses a simple block-allocation strategy on top of big chunks allocated from the Vulkan driver.
 * Does not do any defragging at the moment so will get fragmented over time.
 */

#define VOLO_RVK_MEM_DEBUG 0
#define VOLO_RVK_MEM_LOGGING 0
#define VOLO_RVK_MEM_RELEASE_EMPTY_CHUNKS 1

#define rvk_mem_chunk_size (64 * usize_mebibyte)

typedef u32 RvkChunkId;

struct sRvkMemChunk {
  RvkMemPool*    pool;
  RvkMemChunk*   next;
  RvkChunkId     id;
  RvkMemLoc      loc : 8;
  RvkMemAccess   access : 8;
  u32            size;
  u32            memType;
  DynArray       freeBlocks; // RvkMem[], sorted on offset.
  VkDeviceMemory vkMem;
  void*          map;
};

struct sRvkMemPool {
  VkDevice                         vkDev;
  VkPhysicalDeviceMemoryProperties vkDevMemProps;
  VkPhysicalDeviceLimits           vkDevLimits;
  VkAllocationCallbacks            vkAlloc;
  RvkChunkId                       nextChunkId;
  ThreadMutex                      lock;
  RvkMemChunk*                     chunkHead;
  RvkMemChunk*                     chunkTail;
};

static i8 rend_mem_compare(const void* a, const void* b) {
  return compare_u32(field_ptr(a, RvkMem, offset), field_ptr(b, RvkMem, offset));
}

MAYBE_UNUSED static String rvk_mem_loc_str(const RvkMemLoc loc) {
  switch (loc) {
  case RvkMemLoc_Host:
    return string_lit("host");
  case RvkMemLoc_Dev:
    return string_lit("device");
  }
  diag_crash();
}

MAYBE_UNUSED static String rvk_mem_access_str(const RvkMemAccess access) {
  switch (access) {
  case RvkMemAccess_Linear:
    return string_lit("linear");
  case RvkMemAccess_NonLinear:
    return string_lit("non-linear");
  }
  diag_crash();
}

static VkMemoryPropertyFlagBits rvk_mem_props(const RvkMemLoc loc) {
  switch (loc) {
  case RvkMemLoc_Dev:
    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  case RvkMemLoc_Host:
  default:
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }
}

/**
 * Find a memory-type that is allowed by the mask and that satisfies the requested properties.
 */
static u32 rvk_mem_type(RvkMemPool* pool, const VkMemoryPropertyFlags props, const u32 mask) {
  for (u32 i = 0; i < pool->vkDevMemProps.memoryTypeCount; i++) {
    const bool isAllowed     = (mask & (1 << i)) != 0;
    const bool hasProperties = (pool->vkDevMemProps.memoryTypes[i].propertyFlags & props) == props;
    if (isAllowed && hasProperties) {
      return i;
    }
  }
  diag_crash_msg("Vulkan device has no memory type that satisfies required properties");
}

MAYBE_UNUSED static u32 rvk_mem_end_offset(const RvkMem mem) { return mem.offset + mem.size; }

MAYBE_UNUSED static bool rvk_mem_overlap(const RvkMem a, const RvkMem b) {
  return rvk_mem_end_offset(a) > b.offset && a.offset < rvk_mem_end_offset(b);
}

/**
 * Allocate a continuous block of device memory from Vulkan.
 * NOTE: To avoid gpu memory fragmentation only large blocks should be allocated from Vulkan.
 */
static VkDeviceMemory rvk_mem_alloc_vk(RvkMemPool* pool, const u32 size, const u32 memType) {
  VkMemoryAllocateInfo allocInfo = {
      .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize  = size,
      .memoryTypeIndex = memType,
  };
  VkDeviceMemory result;
  rvk_call(vkAllocateMemory, pool->vkDev, &allocInfo, &pool->vkAlloc, &result);
  return result;
}

/**
 * Free a continuous block of device memory to Vulkan.
 */
static void rvk_mem_free_vk(RvkMemPool* pool, const VkDeviceMemory vkMem) {
  vkFreeMemory(pool->vkDev, vkMem, &pool->vkAlloc);
}

/**
 * Total free bytes in the given chunk.
 */
static u32 rvk_mem_chunk_size_free(const RvkMemChunk* chunk) {
  u32 res = 0;
  dynarray_for_t(&chunk->freeBlocks, RvkMem, freeBlock) { res += freeBlock->size; }
  return res;
}

/**
 * Total occupied bytes in the given chunk.
 */
static u32 rvk_mem_chunk_size_occupied(const RvkMemChunk* chunk) {
  return chunk->size - rvk_mem_chunk_size_free(chunk);
}

MAYBE_UNUSED static bool rvk_mem_chunk_empty(const RvkMemChunk* chunk) {
  return rvk_mem_chunk_size_free(chunk) == chunk->size;
}

/**
 * Verify that all free blocks are correctly sorted.
 */
MAYBE_UNUSED static bool rvk_mem_assert_block_sorting(const RvkMemChunk* chunk) {
  u32 offset = 0;
  dynarray_for_t(&chunk->freeBlocks, RvkMem, freeBlock) {
    diag_assert_msg(
        freeBlock->offset >= offset,
        "Out of order free-block (offset: {}, size: {}) in chunk {}",
        fmt_int(freeBlock->offset),
        fmt_int(freeBlock->size),
        fmt_int(chunk->id));
    offset = freeBlock->offset;
  }
  return offset;
}

static RvkMemChunk* rvk_mem_chunk_create(
    RvkMemPool*        pool,
    const u32          id,
    const RvkMemLoc    loc,
    const RvkMemAccess access,
    const u32          size,
    const u32          memType) {

  RvkMemChunk* chunk = alloc_alloc_t(g_allocHeap, RvkMemChunk);

  *chunk = (RvkMemChunk){
      .id         = id,
      .pool       = pool,
      .loc        = loc,
      .access     = access,
      .size       = size,
      .memType    = memType,
      .freeBlocks = dynarray_create_t(g_allocHeap, RvkMem, 16),
      .vkMem      = rvk_mem_alloc_vk(pool, size, memType),
  };

  if (loc == RvkMemLoc_Host) {
    rvk_call(vkMapMemory, pool->vkDev, chunk->vkMem, 0, VK_WHOLE_SIZE, 0, &chunk->map);
  }

  // Start with a single free block spanning the whole size.
  *dynarray_push_t(&chunk->freeBlocks, RvkMem) = (RvkMem){.size = size};

  diag_assert(rvk_mem_chunk_size_free(chunk) == size);
  diag_assert(rvk_mem_chunk_size_occupied(chunk) == 0);

#if VOLO_RVK_MEM_LOGGING
  log_d(
      "Vulkan memory chunk created",
      log_param("id", fmt_int(chunk->id)),
      log_param("loc", fmt_text(rvk_mem_loc_str(chunk->loc))),
      log_param("access", fmt_text(rvk_mem_access_str(chunk->access))),
      log_param("type", fmt_int(chunk->memType)),
      log_param("size", fmt_size(chunk->size)));
#endif
  return chunk;
}

static void rvk_mem_chunk_destroy(RvkMemChunk* chunk) {
  const u32 leakedBytes = chunk->size - rvk_mem_chunk_size_free(chunk);
  if (UNLIKELY(leakedBytes)) {
    diag_crash_msg(
        "rend mem-pool: {} leaked from chunk (id: {}, loc: {}, access: {}, type: {}, size: {})",
        fmt_size(leakedBytes),
        fmt_int(chunk->id),
        fmt_text(rvk_mem_loc_str(chunk->loc)),
        fmt_text(rvk_mem_access_str(chunk->access)),
        fmt_int(chunk->memType),
        fmt_size(chunk->size));
  }
  diag_assert(rvk_mem_chunk_size_occupied(chunk) == 0);

  rvk_mem_free_vk(chunk->pool, chunk->vkMem);

  dynarray_destroy(&chunk->freeBlocks);
  alloc_free_t(g_allocHeap, chunk);

#if VOLO_RVK_MEM_LOGGING
  log_d(
      "Vulkan memory chunk destroyed",
      log_param("id", fmt_int(chunk->id)),
      log_param("loc", fmt_text(rvk_mem_loc_str(chunk->loc))),
      log_param("access", fmt_text(rvk_mem_access_str(chunk->access))),
      log_param("type", fmt_int(chunk->memType)),
      log_param("size", fmt_size(chunk->size)));
#endif
}

static RvkMem rvk_mem_chunk_alloc(RvkMemChunk* chunk, const u32 size, const u32 align) {
#if VOLO_RVK_MEM_DEBUG
  const u32 dbgFreeSize = rvk_mem_chunk_size_free(chunk);
#endif

  // Find a block that can fit the requested size.
  for (usize i = 0; i != chunk->freeBlocks.size; ++i) {
    RvkMem*   block         = dynarray_at_t(&chunk->freeBlocks, i, RvkMem);
    const u32 offset        = block->offset;
    const u32 padding       = bits_padding_32(offset, align);
    const u32 paddedSize    = size + padding;
    const i32 remainingSize = (i32)block->size - (i32)paddedSize;

    if (remainingSize < 0) {
      continue; // Doesn't fit in this block.
    }

    // Either shrink the block to 'remove' the space, or remove the block entirely.
    if (remainingSize > 0) {
      block->offset += paddedSize;
      block->size = (u32)remainingSize;
    } else {
      dynarray_remove(&chunk->freeBlocks, i, 1);
    }

    if (padding) {
      // Add the lost padding space as a new block.
      *dynarray_insert_t(&chunk->freeBlocks, i, RvkMem) = (RvkMem){
          .offset = offset,
          .size   = padding,
      };
    }

#if VOLO_RVK_MEM_DEBUG
    if (UNLIKELY(dbgFreeSize - rvk_mem_chunk_size_free(chunk) != size)) {
      diag_crash_msg(
          "Memory-pool corrupt after allocate (size: {}, chunk: {}, pre-alloc: {}, post-alloc: {})",
          fmt_int(size),
          fmt_int(chunk->id),
          fmt_int(remainingSize),
          fmt_int(dbgFreeSize),
          fmt_int(rvk_mem_chunk_size_free(chunk)));
    }
    rvk_mem_assert_block_sorting(chunk);
#endif

#if VOLO_RVK_MEM_LOGGING
    log_d(
        "Vulkan memory block allocated",
        log_param("size", fmt_size(size)),
        log_param("align", fmt_int(align)),
        log_param("chunk", fmt_int(chunk->id)));
#endif

    return (RvkMem){.chunk = chunk, .offset = offset + padding, .size = size};
  }

  // No block can fit the requested size.
  return (RvkMem){0};
}

static void rvk_mem_chunk_free(RvkMemChunk* chunk, const RvkMem mem) {
  diag_assert(mem.chunk == chunk);

#if VOLO_RVK_MEM_DEBUG
  dynarray_for_t(&chunk->freeBlocks, RvkMem, freeBlock) {
    if (UNLIKELY(rvk_mem_overlap(*freeBlock, mem))) {
      diag_crash_msg(
          "Memory-pool double-free (size: {}, chunk: {})", fmt_int(mem.size), fmt_int(chunk->id));
    }
  }
  const u32 dbgFreeSize = rvk_mem_chunk_size_free(chunk);
#endif

  // Insert free block.
  *dynarray_insert_sorted_t(&chunk->freeBlocks, RvkMem, rend_mem_compare, &mem) = mem;

  // Merge adjacent free blocks.
  // TODO: Does allot of redundant checks in unchanged free-blocks.
  MAYBE_UNUSED u32 mergedBlocks = 0;
  for (usize i = chunk->freeBlocks.size; i-- > 1;) {
    RvkMem* blockCur  = dynarray_at_t(&chunk->freeBlocks, i, RvkMem);
    RvkMem* blockPrev = dynarray_at_t(&chunk->freeBlocks, i - 1, RvkMem);

    if (rvk_mem_end_offset(*blockPrev) == blockCur->offset) {
      blockPrev->size += blockCur->size;
      dynarray_remove(&chunk->freeBlocks, i, 1);
      ++mergedBlocks;
    }
  }

#if VOLO_RVK_MEM_LOGGING
  log_d(
      "Vulkan memory block freed",
      log_param("size", fmt_size(mem.size)),
      log_param("chunk", fmt_int(chunk->id)),
      log_param("merged-blocks", fmt_int(mergedBlocks)));
#endif

#if VOLO_RVK_MEM_DEBUG
  if (UNLIKELY(rvk_mem_chunk_size_free(chunk) - dbgFreeSize != mem.size)) {
    diag_crash_msg(
        "Memory-pool corrupt after free (size: {}, chunk: {}, pre-free: {}, post-free: {})",
        fmt_size(mem.size),
        fmt_int(chunk->id),
        fmt_int(dbgFreeSize),
        fmt_int(rvk_mem_chunk_size_free(chunk)));
  }
  rvk_mem_assert_block_sorting(chunk);
#endif
}

MAYBE_UNUSED static RvkMemChunk* rvk_mem_pool_chunk_prev(RvkMemPool* pool, RvkMemChunk* chunk) {
  for (RvkMemChunk* prev = pool->chunkHead; prev; prev = prev->next) {
    if (prev->next == chunk) {
      return prev;
    }
  }
  return null;
}

RvkMemPool* rvk_mem_pool_create(
    const VkDevice                         vkDev,
    const VkPhysicalDeviceMemoryProperties props,
    const VkPhysicalDeviceLimits           limits) {
  RvkMemPool* pool = alloc_alloc_t(g_allocHeap, RvkMemPool);

  *pool = (RvkMemPool){
      .vkDev         = vkDev,
      .vkDevMemProps = props,
      .vkDevLimits   = limits,
      .vkAlloc       = rvk_mem_allocator(g_allocHeap),
      .lock          = thread_mutex_create(g_allocHeap),
  };

  return pool;
}

void rvk_mem_pool_destroy(RvkMemPool* pool) {
  for (RvkMemChunk* chunk = pool->chunkHead; chunk;) {
    RvkMemChunk* toDestroy = chunk;
    chunk                  = chunk->next;
    rvk_mem_chunk_destroy(toDestroy);
  }
  thread_mutex_destroy(pool->lock);
  alloc_free_t(g_allocHeap, pool);
}

RvkMem rvk_mem_alloc_req(
    RvkMemPool*                pool,
    const RvkMemLoc            loc,
    const RvkMemAccess         access,
    const VkMemoryRequirements req) {
  diag_assert(req.size <= u32_max);
  diag_assert(req.alignment <= u32_max);
  return rvk_mem_alloc(pool, loc, access, (u32)req.size, (u32)req.alignment, req.memoryTypeBits);
}

RvkMem rvk_mem_alloc(
    RvkMemPool*        pool,
    const RvkMemLoc    loc,
    const RvkMemAccess access,
    const u32          size,
    const u32          align,
    const u32          mask) {

  RvkMem result = {0};
  thread_mutex_lock(pool->lock);

  // Attempt to allocate from an existing chunk.
  for (RvkMemChunk* chunk = pool->chunkHead; chunk; chunk = chunk->next) {
    if (chunk->loc == loc && chunk->access == access && mask & (1 << chunk->memType)) {
      result = rvk_mem_chunk_alloc(chunk, size, align);
      if (rvk_mem_valid(result)) {
        goto Done;
      }
    }
  }

  // No existing chunk has space; create a new chunk.
  const u32        chunkSize = size > rvk_mem_chunk_size ? size : (u32)rvk_mem_chunk_size;
  const u32        memType   = rvk_mem_type(pool, rvk_mem_props(loc), mask);
  const RvkChunkId chunkId   = pool->nextChunkId++;
  RvkMemChunk*     chunk     = rvk_mem_chunk_create(pool, chunkId, loc, access, chunkSize, memType);
  result                     = rvk_mem_chunk_alloc(chunk, size, align);

  if (!pool->chunkHead) {
    pool->chunkHead = chunk;
  }
  if (pool->chunkTail) {
    pool->chunkTail->next = chunk;
  }
  pool->chunkTail = chunk;

Done:
  diag_assert(rvk_mem_valid(result));
  thread_mutex_unlock(pool->lock);
  return result;
}

void rvk_mem_free(const RvkMem mem) {
  diag_assert(rvk_mem_valid(mem));

  RvkMemChunk* chunk = mem.chunk;
  RvkMemPool*  pool  = chunk->pool;

  thread_mutex_lock(pool->lock);

  rvk_mem_chunk_free(chunk, mem);

#if VOLO_RVK_MEM_RELEASE_EMPTY_CHUNKS
  if (rvk_mem_chunk_empty(chunk)) {
    RvkMemChunk* prev = rvk_mem_pool_chunk_prev(pool, chunk);
    if (prev) {
      prev->next = chunk->next;
    }
    if (pool->chunkHead == chunk) {
      pool->chunkHead = chunk->next;
    }
    if (pool->chunkTail == chunk) {
      pool->chunkTail = prev;
    }
#if VOLO_RVK_MEM_LOGGING
    log_d("Vulkan memory chunk released", log_param("id", fmt_int(chunk->id)));
#endif
    rvk_mem_chunk_destroy(chunk);
  }
#endif

  thread_mutex_unlock(pool->lock);
}

void rvk_mem_bind_buffer(const RvkMem mem, const VkBuffer vkBuffer) {
  diag_assert(rvk_mem_valid(mem));
  rvk_call(vkBindBufferMemory, mem.chunk->pool->vkDev, vkBuffer, mem.chunk->vkMem, mem.offset);
}

void rvk_mem_bind_image(const RvkMem mem, const VkImage vkImage) {
  diag_assert(rvk_mem_valid(mem));
  rvk_call(vkBindImageMemory, mem.chunk->pool->vkDev, vkImage, mem.chunk->vkMem, mem.offset);
}

Mem rvk_mem_map(const RvkMem mem) {
  diag_assert(rvk_mem_valid(mem));
  const void* baseMapPtr = mem.chunk->map;
  return mem_create(bits_ptr_offset(baseMapPtr, mem.offset), mem.size);
}

void rvk_mem_flush(const RvkMem mem, const u32 offset, const u32 size) {
  const RvkMemFlush flushes[] = {
      {.mem = mem, .offset = offset, .size = size},
  };
  rvk_mem_flush_batch(flushes, array_elems(flushes));
}

void rvk_mem_flush_batch(const RvkMemFlush flushes[], const u32 count) {
  if (!count) {
    return;
  }
  const RvkMemPool* pool           = flushes[0].mem.chunk->pool;
  const u32         flushAlignment = (u32)pool->vkDevLimits.nonCoherentAtomSize;

  VkMappedMemoryRange* ranges = mem_stack(sizeof(VkMappedMemoryRange) * count).ptr;
  for (u32 i = 0; i != count; ++i) {
    const RvkMemFlush* flush = &flushes[i];
    diag_assert(rvk_mem_valid(flush->mem));

    const RvkMemChunk* chunk = flush->mem.chunk;
    diag_assert(chunk->map); // Only mapped memory can be flushed.
    diag_assert(chunk->pool == pool);

    const u32 chunkOffset = flush->mem.offset + flush->offset;
    diag_assert(chunkOffset + flush->size <= flush->mem.offset + flush->mem.size);

    // Align the offset to be a multiple of 'flushAlignment'.
    const u32 alignedOffset = chunkOffset / flushAlignment * flushAlignment;
    diag_assert(chunkOffset >= alignedOffset && chunkOffset - alignedOffset < flushAlignment);

    // Pad the size to be aligned (or until the end of the chunk).
    u32 paddedSize = bits_align(flush->size, flushAlignment);
    if (chunkOffset + paddedSize > chunk->size) {
      paddedSize = chunk->size - chunkOffset;
    }

    ranges[i] = (VkMappedMemoryRange){
        .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = chunk->vkMem,
        .offset = alignedOffset,
        .size   = paddedSize,
    };
  }
  rvk_call(vkFlushMappedMemoryRanges, pool->vkDev, count, ranges);
}

u64 rvk_mem_occupied(const RvkMemPool* pool, const RvkMemLoc loc) {
  thread_mutex_lock(pool->lock);

  u64 occupied = 0;
  for (RvkMemChunk* chunk = pool->chunkHead; chunk; chunk = chunk->next) {
    if (chunk->loc == loc) {
      occupied += rvk_mem_chunk_size_occupied(chunk);
    }
  }

  thread_mutex_unlock(pool->lock);
  return occupied;
}

u64 rvk_mem_reserved(const RvkMemPool* pool, const RvkMemLoc loc) {
  thread_mutex_lock(pool->lock);

  u64 reserved = 0;
  for (RvkMemChunk* chunk = pool->chunkHead; chunk; chunk = chunk->next) {
    if (chunk->loc == loc) {
      reserved += chunk->size;
    }
  }

  thread_mutex_unlock(pool->lock);
  return reserved;
}

u16 rvk_mem_chunks(const RvkMemPool* pool) {
  thread_mutex_lock(pool->lock);

  u16 chunks = 0;
  for (RvkMemChunk* chunk = pool->chunkHead; chunk; chunk = chunk->next) {
    ++chunks;
  }

  thread_mutex_unlock(pool->lock);
  return chunks;
}
