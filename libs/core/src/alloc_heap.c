#include "core_alloc.h"
#include "core_annotation.h"
#include "core_bits.h"

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
} AllocatorHeap;

static usize alloc_heap_pow_index(const usize size) {
  const usize sizePow2 = bits_nextpow2(size);
  return bits_ctz(sizePow2);
}

static Allocator* alloc_heap_sub_allocator(AllocatorHeap* allocHeap, const usize size) {
  const usize powIdx = alloc_heap_pow_index(size);
  if (UNLIKELY(powIdx < block_bucket_pow_min)) {
    return allocHeap->blockBuckets[0];
  }
  if (UNLIKELY(powIdx > block_bucket_pow_max)) {
    return g_alloc_page;
  }
  return allocHeap->blockBuckets[powIdx - block_bucket_pow_min];
}

static Mem alloc_heap_alloc(Allocator* allocator, const usize size, const usize align) {
  AllocatorHeap* allocHeap = (AllocatorHeap*)allocator;
  Allocator*     allocSub  = alloc_heap_sub_allocator(allocHeap, size);
  return alloc_alloc(allocSub, size, align);
}

static void alloc_heap_free(Allocator* allocator, Mem mem) {
  AllocatorHeap* allocHeap = (AllocatorHeap*)allocator;
  Allocator*     allocSub  = alloc_heap_sub_allocator(allocHeap, mem.size);
  alloc_free(allocSub, mem);
}

static usize alloc_heap_max_size(Allocator* allocator) {
  (void)allocator;
  return alloc_max_alloc_size;
}

static AllocatorHeap g_allocatorIntern;

Allocator* alloc_heap_init() {
  g_allocatorIntern = (AllocatorHeap){
      (Allocator){
          .alloc   = alloc_heap_alloc,
          .free    = alloc_heap_free,
          .maxSize = alloc_heap_max_size,
          .reset   = null,
      },
      .blockBuckets = {0},
  };
  for (usize i = 0; i != block_bucket_count; ++i) {
    const usize blockSize             = usize_lit(1) << (i + block_bucket_pow_min);
    g_allocatorIntern.blockBuckets[i] = alloc_block_create(g_alloc_page, blockSize);
  }
  return (Allocator*)&g_allocatorIntern;
}

void alloc_heap_teardown() {
  for (usize i = 0; i != block_bucket_count; ++i) {
    Allocator* allocBlock = g_allocatorIntern.blockBuckets[i];

    const usize leakedBlocks = alloc_block_allocated_blocks(allocBlock);
    if (leakedBlocks) {
      alloc_crash_with_msg(
          "heap: {} allocations leaked in size-class {} during app runtime",
          fmt_int(leakedBlocks),
          fmt_size(alloc_max_size(allocBlock)));
    }

    alloc_block_destroy(allocBlock);
  }
}
