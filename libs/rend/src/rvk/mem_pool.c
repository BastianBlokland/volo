#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "mem_internal.h"

/**
 * Device memory pool.
 *
 * Uses a simple block-allocation strategy on top of big chunks allocated from the Vulkan driver.
 * Does not do any defragging at the moment so will get fragmented over time.
 */

// #define VOLO_RVK_MEM_DEBUG
// #define VOLO_RVK_MEM_LOGGING

#define rvk_mem_chunk_size (64 * usize_mebibyte)

typedef u32 RvkChunkId;

struct sRvkMemChunk {
  RvkChunkId     id;
  RvkMemPool*    pool;
  RvkMemChunk*   next;
  RvkMemLoc      loc;
  RvkMemAccess   access;
  u64            size;
  u32            memType;
  DynArray       freeBlocks; // RvkMem[]
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

MAYBE_UNUSED static u64 rvk_mem_end_offset(const RvkMem mem) { return mem.offset + mem.size; }

MAYBE_UNUSED static bool rvk_mem_overlap(const RvkMem a, const RvkMem b) {
  return rvk_mem_end_offset(a) > b.offset && a.offset < rvk_mem_end_offset(b);
}

/**
 * Allocate a continuous block of device memory from Vulkan.
 * NOTE: To avoid gpu memory fragmention only large blocks should be allocated from Vulkan.
 */
static VkDeviceMemory rvk_mem_alloc_vk(RvkMemPool* pool, const u64 size, const u32 memType) {
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
static u64 rvk_mem_chunk_size_free(const RvkMemChunk* chunk) {
  u64 res = 0;
  dynarray_for_t(&chunk->freeBlocks, RvkMem, freeBlock) { res += freeBlock->size; }
  return res;
}

/**
 * Total occupied bytes in the given chunk.
 */
static u64 rvk_mem_chunk_size_occupied(const RvkMemChunk* chunk) {
  return chunk->size - rvk_mem_chunk_size_free(chunk);
}

static RvkMemChunk* rvk_mem_chunk_create(
    RvkMemPool*        pool,
    const u32          id,
    const RvkMemLoc    loc,
    const RvkMemAccess access,
    const u64          size,
    const u32          memType) {

  RvkMemChunk* chunk = alloc_alloc_t(g_alloc_heap, RvkMemChunk);
  *chunk             = (RvkMemChunk){
      .id         = id,
      .pool       = pool,
      .loc        = loc,
      .access     = access,
      .size       = size,
      .memType    = memType,
      .freeBlocks = dynarray_create_t(g_alloc_heap, RvkMem, 16),
      .vkMem      = rvk_mem_alloc_vk(pool, size, memType),
  };
  if (loc == RvkMemLoc_Host) {
    rvk_call(vkMapMemory, pool->vkDev, chunk->vkMem, 0, VK_WHOLE_SIZE, 0, &chunk->map);
  }

  // Start with a single free block spanning the whole size.
  *dynarray_push_t(&chunk->freeBlocks, RvkMem) = (RvkMem){.size = size};

  diag_assert(rvk_mem_chunk_size_free(chunk) == size);
  diag_assert(rvk_mem_chunk_size_occupied(chunk) == 0);

#ifdef VOLO_RVK_MEM_LOGGING
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

  const u64 leakedBytes = chunk->size - rvk_mem_chunk_size_free(chunk);
  if (UNLIKELY(leakedBytes)) {
    diag_crash_msg("rend mem-pool: {} leaked from chunk", fmt_size(leakedBytes));
  }
  diag_assert(rvk_mem_chunk_size_occupied(chunk) == 0);

  rvk_mem_free_vk(chunk->pool, chunk->vkMem);

  dynarray_destroy(&chunk->freeBlocks);
  alloc_free_t(g_alloc_heap, chunk);

#ifdef VOLO_RVK_MEM_LOGGING
  log_d(
      "Vulkan memory chunk destroyed",
      log_param("id", fmt_int(chunk->id)),
      log_param("loc", fmt_text(rvk_mem_loc_str(chunk->loc))),
      log_param("access", fmt_text(rvk_mem_access_str(chunk->access))),
      log_param("type", fmt_int(chunk->memType)),
      log_param("size", fmt_size(chunk->size)));
#endif
}

static RvkMem rvk_mem_chunk_alloc(RvkMemChunk* chunk, const u64 size, const u64 align) {

#ifdef VOLO_RVK_MEM_DEBUG
  const u64 dbgFreeSize = rvk_mem_chunk_size_free(chunk);
#endif

  // Find a block that can fit the requested size.
  for (usize i = 0; i != chunk->freeBlocks.size; ++i) {
    RvkMem*   block         = dynarray_at_t(&chunk->freeBlocks, i, RvkMem);
    const u64 offset        = block->offset;
    const u64 padding       = bits_padding_64(offset, align);
    const u64 paddedSize    = size + padding;
    const i64 remainingSize = (i64)block->size - (i64)paddedSize;

    if (remainingSize < 0) {
      // Doesn't fit in this block.
      continue;
    }

    if (padding) {
      // Add the lost padding space as a new block.
      *dynarray_push_t(&chunk->freeBlocks, RvkMem) = (RvkMem){.offset = offset, .size = padding};
    }

    // Either shrink the block to 'remove' the space, or remove the block entirely.
    if (remainingSize > 0) {
      block->offset += paddedSize;
      block->size = remainingSize;
    } else {
      dynarray_remove_unordered(&chunk->freeBlocks, i, 1);
    }

#ifdef VOLO_RVK_MEM_DEBUG
    diag_assert(dbgFreeSize - rvk_mem_chunk_size_free(chunk) == size);
#endif

#ifdef VOLO_RVK_MEM_LOGGING
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

#ifdef VOLO_RVK_MEM_DEBUG
  // Check that this block was not freed before.
  dynarray_for_t(&chunk->freeBlocks, RvkMem, freeBlock) {
    diag_assert(!rvk_mem_overlap(*freeBlock, mem));
  }
  const u64 dbgFreeSize = rvk_mem_chunk_size_free(chunk);
#endif

  // Check if there already is a free block before or after this one, if so then 'grow' that.
  // TODO: Merge blocks if they become adjacent due to the given block being freed.
  dynarray_for_t(&chunk->freeBlocks, RvkMem, freeBlock) {
    // Check if this freeBlock is right before the given block.
    if (rvk_mem_end_offset(*freeBlock) == mem.offset) {
      freeBlock->size += mem.size;
      goto Done;
    }

    // Check if this freeBlock is right after the given block.
    if (freeBlock->offset == rvk_mem_end_offset(mem)) {
      freeBlock->offset -= mem.size;
      freeBlock->size += mem.size;
      goto Done;
    }
  }

  // No block to join, add as a new block.
  *dynarray_push_t(&chunk->freeBlocks, RvkMem) = (RvkMem){.offset = mem.offset, .size = mem.size};

Done:
  (void)0;

#ifdef VOLO_RVK_MEM_LOGGING
  log_d(
      "Vulkan memory block freed",
      log_param("size", fmt_int(mem.size)),
      log_param("chunk", fmt_int(chunk->id)));
#endif

#ifdef VOLO_RVK_MEM_DEBUG
  diag_assert(rvk_mem_chunk_size_free(chunk) - dbgFreeSize == mem.size);
#endif
}

static void rvk_mem_chunk_flush(RvkMemChunk* chunk, u64 offset, u64 size) {
  diag_assert(chunk->map); // Only mapped memory can be flushed.

  const u64 flushAlignment = chunk->pool->vkDevLimits.nonCoherentAtomSize;

  // Align the offset to be a multiple of 'flushAlignment'.
  const u64 alignedOffset = offset / flushAlignment * flushAlignment;
  diag_assert(offset >= alignedOffset && offset - alignedOffset < flushAlignment);

  // Pad the size to be aligned (or until the end of the chunk).
  u64 paddedSize = bits_align(size, flushAlignment);
  if (offset + paddedSize > chunk->size) {
    paddedSize = chunk->size - offset;
  }

  VkMappedMemoryRange mappedMemoryRange = {
      .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory = chunk->vkMem,
      .offset = alignedOffset,
      .size   = paddedSize,
  };
  rvk_call(vkFlushMappedMemoryRanges, chunk->pool->vkDev, 1, &mappedMemoryRange);
}

RvkMemPool* rvk_mem_pool_create(
    const VkDevice                         vkDev,
    const VkPhysicalDeviceMemoryProperties props,
    const VkPhysicalDeviceLimits           limits) {
  RvkMemPool* pool = alloc_alloc_t(g_alloc_heap, RvkMemPool);
  *pool            = (RvkMemPool){
      .vkDev         = vkDev,
      .vkDevMemProps = props,
      .vkDevLimits   = limits,
      .vkAlloc       = rvk_mem_allocator(g_alloc_heap),
      .lock          = thread_mutex_create(g_alloc_heap),
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
  alloc_free_t(g_alloc_heap, pool);
}

RvkMem rvk_mem_alloc_req(
    RvkMemPool*                pool,
    const RvkMemLoc            loc,
    const RvkMemAccess         access,
    const VkMemoryRequirements req) {
  return rvk_mem_alloc(pool, loc, access, req.size, req.alignment, req.memoryTypeBits);
}

RvkMem rvk_mem_alloc(
    RvkMemPool*        pool,
    const RvkMemLoc    loc,
    const RvkMemAccess access,
    const u64          size,
    const u64          align,
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
  const u64        chunkSize = size > rvk_mem_chunk_size ? size : (u64)rvk_mem_chunk_size;
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

  // NOTE: Add per chunk locks would prevent needing to lock the entire pool here.
  thread_mutex_lock(mem.chunk->pool->lock);
  rvk_mem_chunk_free(mem.chunk, mem);
  thread_mutex_unlock(mem.chunk->pool->lock);
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

void rvk_mem_flush(const RvkMem mem) {
  diag_assert(rvk_mem_valid(mem));
  rvk_mem_chunk_flush(mem.chunk, mem.offset, mem.size);
}
