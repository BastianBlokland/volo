#include "core_diag.h"

#include "alloc_internal.h"

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

  // NOTE: Tag the memory to detect UAF, could be tied to a define in the future.
  alloc_tag_free(mem, AllocMemType_Normal);

  if (mem_end(mem) == allocatorBump->head) {
    // This was the last allocation made, we can 'unbump' it.
    allocatorBump->head -= mem.size;
  }
}

static usize alloc_bump_max_size(Allocator* allocator) {
  struct AllocatorBump* allocatorBump = (struct AllocatorBump*)allocator;
  return allocatorBump->tail - allocatorBump->head;
}

static void alloc_bump_reset(Allocator* allocator) {
  struct AllocatorBump* allocatorBump = (struct AllocatorBump*)allocator;
  allocatorBump->head                 = bits_ptr_offset(allocator, sizeof(struct AllocatorBump));

  // NOTE: Tag the memory to detect UAF, could be tied to a define in the future.
  alloc_tag_guard(mem_from_to(allocatorBump->head, allocatorBump->tail), AllocMemType_Normal);
}

Allocator* alloc_bump_create(Mem mem) {
  if (mem.size <= sizeof(struct AllocatorBump)) {
    return null; // Too little space for our internal bookkeeping.
  }
  struct AllocatorBump* allocatorBump = mem_as_t(mem, struct AllocatorBump);

  allocatorBump->api = (Allocator){
      .alloc   = alloc_bump_alloc,
      .free    = alloc_bump_free,
      .maxSize = alloc_bump_max_size,
      .reset   = alloc_bump_reset,
  };
  allocatorBump->head = mem_begin(mem) + sizeof(struct AllocatorBump);
  allocatorBump->tail = mem_end(mem);
  return (Allocator*)allocatorBump;
}
