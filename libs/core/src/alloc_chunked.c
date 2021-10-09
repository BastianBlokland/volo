#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"

#include "alloc_internal.h"

#define alloc_chunk_size_min 128
#define alloc_chunk_align sizeof(void*)
#define alloc_chunks_max 32

typedef struct {
  Allocator        api;
  Allocator*       parent;
  AllocatorBuilder builder;
  Allocator*       preferredChunk;
  usize            chunkSize, chunkCount;
  Allocator*       chunks[alloc_chunks_max];
} AllocatorChunked;

static Allocator* alloc_chunk_create(AllocatorChunked* alloc) {
  Mem chunkMem = alloc_alloc(alloc->parent, alloc->chunkSize, alloc_chunk_align);
  return alloc->builder(chunkMem);
}

static void alloc_chunk_destroy(AllocatorChunked* alloc, Allocator* chunk) {
  alloc_free(alloc->parent, mem_create(chunk, alloc->chunkSize));
}

/**
 * Check if the given memory belongs to this chunk.
 */
static bool alloc_chunk_contains(AllocatorChunked* alloc, Allocator* chunk, Mem mem) {
  void* chunkHead = chunk;
  void* chunkTail = bits_ptr_offset(chunkHead, alloc->chunkSize);
  return mem.ptr >= chunkHead && mem.ptr < chunkTail;
}

static Mem alloc_chunked_alloc(Allocator* allocator, const usize size, const usize align) {
  AllocatorChunked* alloc = (AllocatorChunked*)allocator;

  /**
   * Keep track of a preferred-chunk and always try to allocate from that.
   * If the preferred-chunk has no space left then we mark the first chunk with space as the new
   * preferred-chunk.
   */

  if (LIKELY(alloc->preferredChunk)) {
    Mem mem = alloc_alloc(alloc->preferredChunk, size, align);
    if (LIKELY(mem_valid(mem))) {
      return mem;
    }
  }

  for (usize i = 0; i != alloc->chunkCount; ++i) {
    Mem mem = alloc_alloc(alloc->chunks[i], size, align);
    if (mem_valid(mem)) {
      alloc->preferredChunk = alloc->chunks[i];
      return mem;
    }
  }

  if (UNLIKELY(alloc->chunkCount == alloc_chunks_max)) {
    // Maximum chunks reached; fail the allocation.
    alloc->preferredChunk = null;
    return mem_create(null, size);
  }

  alloc->preferredChunk              = alloc_chunk_create(alloc);
  alloc->chunks[alloc->chunkCount++] = alloc->preferredChunk;
  return alloc_alloc(alloc->preferredChunk, size, align);
}

static void alloc_chunked_free(Allocator* allocator, Mem mem) {
  diag_assert(mem_valid(mem));

  AllocatorChunked* alloc = (AllocatorChunked*)allocator;

  /**
   * NOTE: Would it make sense to first try to free from the preferred allocator?
   */

  for (usize i = 0; i != alloc->chunkCount; ++i) {
    Allocator* chunk = alloc->chunks[i];
    if (alloc_chunk_contains(alloc, chunk, mem)) {
      alloc_free(chunk, mem);
      break;
    }
  }
}

static usize alloc_chunked_min_size(Allocator* allocator) {
  (void)allocator;
  return 1;
}

static usize alloc_chunked_max_size(Allocator* allocator) {
  AllocatorChunked* alloc = (AllocatorChunked*)allocator;

  usize maxSize = 0;
  for (usize i = 0; i != alloc->chunkCount; ++i) {
    maxSize = math_max(maxSize, alloc_max_size(alloc->chunks[i]));
  }
  return maxSize;
}

static void alloc_chunked_reset(Allocator* allocator) {
  AllocatorChunked* alloc = (AllocatorChunked*)allocator;

  alloc->preferredChunk = null;
  for (usize i = 0; i != alloc->chunkCount; ++i) {
    alloc_reset(alloc->chunks[i]);
  }
}

Allocator* alloc_chunked_create(Allocator* parent, AllocatorBuilder builder, usize chunkSize) {
  diag_assert_msg(
      chunkSize >= alloc_chunk_size_min,
      "Chunk-size '{}' is less then the minimum of '{}'",
      fmt_size(chunkSize),
      fmt_size(alloc_chunk_size_min));
  diag_assert_msg(
      bits_ispow2(chunkSize), "Chunk-size '{}' is not a power-of-two", fmt_int(chunkSize));

  /**
   * The control-data is a separate allocation using the 'g_alloc_heap' allocator. Alternatively we
   * could store the control-data in the first allocated chunk, but this would require additional
   * book-keeping.
   */
  AllocatorChunked* alloc = alloc_alloc_t(g_alloc_heap, AllocatorChunked);
  *alloc                  = (AllocatorChunked){
      .api =
          {
              .alloc   = alloc_chunked_alloc,
              .free    = alloc_chunked_free,
              .minSize = alloc_chunked_min_size,
              .maxSize = alloc_chunked_max_size,
              .reset   = alloc_chunked_reset,
          },
      .parent    = parent,
      .builder   = builder,
      .chunkSize = chunkSize,
  };
  return (Allocator*)alloc;
}

void alloc_chunked_destroy(Allocator* allocator) {
  diag_assert_msg(allocator, "Allocator not initialized");

  AllocatorChunked* allocatorChunked = (AllocatorChunked*)allocator;

  for (usize i = 0; i != allocatorChunked->chunkCount; ++i) {
    alloc_chunk_destroy(allocatorChunked, allocatorChunked->chunks[i]);
  }

  alloc_free_t(g_alloc_heap, allocatorChunked);
}
