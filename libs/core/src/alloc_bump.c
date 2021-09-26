#include "core_diag.h"

#include "alloc_internal.h"

#define freed_mem_tag 0xFB
#define guard_mem_tag 0xAB

struct AllocatorBump {
  Allocator api;
  u8*       head;
  u8*       tail;
};

static Mem alloc_bump_alloc(Allocator* allocator, const usize size, const usize align) {
  struct AllocatorBump* allocatorBump = (struct AllocatorBump*)allocator;

  u8* alignedHead = bits_align_ptr(allocatorBump->head, align);

  if (UNLIKELY((usize)(allocatorBump->tail - alignedHead) < size)) {
    // Too little space remaining.
    return mem_create(null, size);
  }

  allocatorBump->head = alignedHead + size;
  return mem_create(alignedHead, size);
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

static void alloc_bump_reset(Allocator* allocator) {
  struct AllocatorBump* allocatorBump = (struct AllocatorBump*)allocator;
  allocatorBump->head                 = bits_ptr_offset(allocator, sizeof(struct AllocatorBump));

  // TODO: Create special compiler define to enable / disable tagging of reset mem.
  mem_set(mem_from_to(allocatorBump->head, allocatorBump->tail), guard_mem_tag);
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
      .reset   = alloc_bump_reset,
  };
  allocatorBump->head = mem_begin(mem) + sizeof(struct AllocatorBump);
  allocatorBump->tail = mem_end(mem);
  return (Allocator*)allocatorBump;
}
