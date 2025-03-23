#include "core.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_file.h"
#include "core_thread.h"

#include "alloc_internal.h"

#define block_bucket_pow_min 4
#define block_bucket_pow_max 11
#define block_bucket_size_min (usize_lit(1) << block_bucket_pow_min)
#define block_bucket_size_max (usize_lit(1) << block_bucket_pow_max)
#define block_bucket_count (block_bucket_pow_max - block_bucket_pow_min + 1)

ASSERT(block_bucket_size_min == 16, "Unexpected bucket min size");
ASSERT(block_bucket_size_max == 2048, "Unexpected bucket max size");
ASSERT(block_bucket_count == 8, "Unexpected bucket count");

typedef struct {
  Allocator  api;
  Allocator* blockBuckets[block_bucket_count];

#ifdef VOLO_MEMORY_TRACKING
  AllocTracker* tracker;
#endif
  i64 counter; // Incremented on every allocation.
} AllocatorHeap;

static usize alloc_heap_pow_index(const usize size) {
  const usize sizePow2 = sized_call(bits_nextpow2, size);
  return sized_call(bits_ctz, sizePow2);
}

static Allocator* alloc_heap_sub_allocator(AllocatorHeap* allocHeap, const usize size) {
  const usize powIdx = alloc_heap_pow_index(size);
  if (UNLIKELY(powIdx < block_bucket_pow_min)) {
    return allocHeap->blockBuckets[0];
  }
  if (UNLIKELY(powIdx > block_bucket_pow_max)) {
    return g_allocPageCache;
  }
  return allocHeap->blockBuckets[powIdx - block_bucket_pow_min];
}

static Mem alloc_heap_alloc(Allocator* allocator, const usize size, const usize align) {
  AllocatorHeap* allocHeap = (AllocatorHeap*)allocator;
  Allocator*     allocSub  = alloc_heap_sub_allocator(allocHeap, size);
  thread_atomic_add_i64(&allocHeap->counter, 1);

  const Mem result = alloc_alloc(allocSub, size, align);
#ifdef VOLO_MEMORY_TRACKING
  if (LIKELY(mem_valid(result))) {
    alloc_tracker_add(allocHeap->tracker, result, symbol_stack_walk());
  }
#endif
  return result;
}

static void alloc_heap_free(Allocator* allocator, const Mem mem) {
  AllocatorHeap* allocHeap = (AllocatorHeap*)allocator;
  Allocator*     allocSub  = alloc_heap_sub_allocator(allocHeap, mem.size);
#ifdef VOLO_MEMORY_TRACKING
  alloc_tracker_remove(allocHeap->tracker, mem);
#endif
  alloc_free(allocSub, mem);
}

static usize alloc_heap_max_size(Allocator* allocator) {
  (void)allocator;
  return alloc_max_alloc_size;
}

static AllocatorHeap g_allocatorIntern;

Allocator* alloc_heap_init(void) {
  g_allocatorIntern = (AllocatorHeap){
      (Allocator){
          .alloc   = alloc_heap_alloc,
          .free    = alloc_heap_free,
          .maxSize = alloc_heap_max_size,
          .reset   = null,
      },
#ifdef VOLO_MEMORY_TRACKING
      .tracker = alloc_tracker_create(),
#endif
      .blockBuckets = {0},
  };
  for (usize i = 0; i != block_bucket_count; ++i) {
    const usize blockSize             = usize_lit(1) << (i + block_bucket_pow_min);
    g_allocatorIntern.blockBuckets[i] = alloc_block_create(g_allocPageCache, blockSize, blockSize);
  }
  return (Allocator*)&g_allocatorIntern;
}

void alloc_heap_leak_detect(void) {
#ifdef VOLO_MEMORY_TRACKING
  const usize leakedAllocations = alloc_tracker_count(g_allocatorIntern.tracker);
  if (UNLIKELY(leakedAllocations)) {
    alloc_tracker_dump_file(g_allocatorIntern.tracker, g_fileStdErr);
    diag_crash_msg("heap: leaked {} allocation(s)", fmt_int(leakedAllocations));
  }
#endif
}

void alloc_heap_teardown(void) {
  for (usize i = 0; i != block_bucket_count; ++i) {
    alloc_block_destroy(g_allocatorIntern.blockBuckets[i]);
  }
#ifdef VOLO_MEMORY_TRACKING
  alloc_tracker_destroy(g_allocatorIntern.tracker);
#endif
  g_allocatorIntern = (AllocatorHeap){0};
}

u64 alloc_heap_active(void) {
#ifdef VOLO_MEMORY_TRACKING
  return alloc_tracker_count(g_allocatorIntern.tracker);
#else
  /**
   * NOTE: Without the memory tracker we estimate the active allocations by summing the allocations
   * in the block allocators. This misses the big allocs that we forwarded to the page allocator.
   */
  u64 result = 0;
  for (usize i = 0; i != block_bucket_count; ++i) {
    Allocator* allocBlock = g_allocatorIntern.blockBuckets[i];
    result += alloc_block_allocated_blocks(allocBlock);
  }
  return result;
#endif
}

u64 alloc_heap_counter(void) { return (u64)thread_atomic_load_i64(&g_allocatorIntern.counter); }

void alloc_heap_dump(void) {
#ifdef VOLO_MEMORY_TRACKING
  alloc_tracker_dump_file(g_allocatorIntern.tracker, g_fileStdOut);
#endif
}
