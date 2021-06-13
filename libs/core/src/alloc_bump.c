#include "alloc_internal.h"
#include "core_diag.h"

#define freed_mem_tag 0xFF

struct AllocatorBump {
  Allocator api;
  u8*       head;
  u8*       tail;
};

static Mem alloc_bump_alloc(Allocator* allocator, const usize size) {
  diag_assert(size);

  struct AllocatorBump* allocatorBump = (struct AllocatorBump*)allocator;
  if (UNLIKELY((usize)(allocatorBump->tail - allocatorBump->head) < size)) {
    // Too little space remaining.
    return mem_create(null, size);
  }

  void* res = allocatorBump->head;
  allocatorBump->head += size;
  return mem_create(res, size);
}

static void alloc_bump_free(Allocator* allocator, Mem mem) {
  diag_assert(mem_valid(mem));

  struct AllocatorBump* allocatorBump = (struct AllocatorBump*)allocator;

  // TODO: Create special compiler define to enable / disable tagging of freed mem.
  mem_set(mem, freed_mem_tag); // Tag to detect use-after-free.

  if (mem_end(mem) == allocatorBump->head) {
    // This was the last allocation made, we can 'unbump' it.
    allocatorBump->head -= mem.size;
  }
}

static usize alloc_bump_min_size(Allocator* allocator) {
  (void)allocator;
  return 1;
}

static usize alloc_bump_max_size(Allocator* allocator) {
  struct AllocatorBump* allocatorBump = (struct AllocatorBump*)allocator;
  return allocatorBump->tail - allocatorBump->head;
}

Allocator* alloc_bump_create(Mem mem) {
  if (mem.size <= sizeof(struct AllocatorBump)) {
    return null; // Too little space for our internal bookkeeping.
  }
  struct AllocatorBump* allocatorBump = mem_as_t(mem, struct AllocatorBump);

  allocatorBump->api = (Allocator){
      .alloc   = alloc_bump_alloc,
      .free    = alloc_bump_free,
      .minSize = alloc_bump_min_size,
      .maxSize = alloc_bump_max_size,
  };
  allocatorBump->head = mem_begin(mem) + sizeof(struct AllocatorBump);
  allocatorBump->tail = mem_end(mem);
  return (Allocator*)allocatorBump;
}
