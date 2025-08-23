#include "core/bits.h"
#include "core/diag.h"

#include "alloc.h"

/**
 * Tag the entire memory region on reset to help detecting 'Use After Free'.
 */
#define bump_reset_guard_enable 0

struct AllocatorBump {
  Allocator api;
  u8*       head;
  u8*       tail;
};

ASSERT(sizeof(struct AllocatorBump) <= 64, "Bump allocator too big");

/**
 * Pre-condition: bits_ispow2(_ALIGN_)
 */
INLINE_HINT static u8* alloc_bump_align_ptr(u8* ptr, const usize align) {
  return (u8*)((uptr)ptr + ((~(uptr)ptr + 1) & (align - 1)));
}

static Mem alloc_bump_alloc(Allocator* allocator, const usize size, const usize align) {
  struct AllocatorBump* allocatorBump = (struct AllocatorBump*)allocator;

  u8* alignedHead = alloc_bump_align_ptr(allocatorBump->head, align);

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

  // NOTE: Tag the memory to detect UAF.
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

#if bump_reset_guard_enable
  alloc_tag_guard(mem_from_to(allocatorBump->head, allocatorBump->tail), AllocMemType_Normal);
#endif
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
