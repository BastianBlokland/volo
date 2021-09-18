#include "core_alloc.h"
#include "core_diag.h"

#include "alloc_internal.h"

#define alloc_scratch_heap_size (usize_mebibyte * 4)
#define alloc_scratch_max_alloc_size (usize_kibibyte * 64)
#define alloc_scratch_guard_size (usize_kibibyte * 128)

#define freed_mem_tag 0xFC
#define guard_mem_tag 0xAA

struct AllocatorScratch {
  Allocator api;
  Mem       memory;
  u8*       head;
};

MAYBE_UNUSED static void alloc_scratch_write_guard(struct AllocatorScratch* allocator) {
  const usize memUntilEnd = mem_end(allocator->memory) - allocator->head;
  if (memUntilEnd > alloc_scratch_guard_size) {
    mem_set(mem_create(allocator->head, alloc_scratch_guard_size), guard_mem_tag);
  } else {
    mem_set(mem_create(allocator->head, memUntilEnd), guard_mem_tag);
    mem_set(mem_create(mem_begin(allocator->memory), alloc_scratch_guard_size), guard_mem_tag);
  }
}

static Mem alloc_scratch_alloc(Allocator* allocator, const usize size, const usize align) {

  struct AllocatorScratch* allocatorScratch = (struct AllocatorScratch*)allocator;

  if (UNLIKELY(size > alloc_scratch_max_alloc_size)) {
    // Too big allocation, we limit the maximum allocation size to avoid 'invalidating' too many
    // other scratch allocations at once.
    return mem_create(null, size);
  }

  u8* alignedHead = (u8*)bits_align((uptr)allocatorScratch->head, align);

  if (UNLIKELY(alignedHead + size > mem_end(allocatorScratch->memory))) {
    // Wrap around the scratch buffer.
    alignedHead = (u8*)bits_align((uptr)mem_begin(allocatorScratch->memory), align);
  }

  allocatorScratch->head = alignedHead + size;

  // TODO: Create special compiler define to enable / disable the guard region.
  // Write a special tag to the memory ahead of the head, usefull to detect cases where the
  // application is holding on to scratch memory that is about to be overwritten.
  alloc_scratch_write_guard(allocatorScratch);

  return mem_create(alignedHead, size);
}

static void alloc_scratch_free(Allocator* allocator, Mem mem) {
  (void)allocator;
  (void)mem;

  diag_assert(mem_valid(mem));

  // TODO: Create special compiler define to enable / disable tagging of freed mem.
  mem_set(mem, freed_mem_tag); // Tag to detect use-after-free.
}

static usize alloc_scratch_min_size(Allocator* allocator) {
  (void)allocator;
  return 1;
}

static usize alloc_scratch_max_size(Allocator* allocator) {
  (void)allocator;
  return alloc_scratch_max_alloc_size;
}

static THREAD_LOCAL struct AllocatorScratch g_allocatorIntern;

Allocator* alloc_scratch_init() {
  Mem scratchPages  = alloc_alloc(g_alloc_page, alloc_scratch_heap_size, sizeof(void*));
  g_allocatorIntern = (struct AllocatorScratch){
      (Allocator){
          .alloc   = alloc_scratch_alloc,
          .free    = alloc_scratch_free,
          .minSize = alloc_scratch_min_size,
          .maxSize = alloc_scratch_max_size,
      },
      .memory = scratchPages,
      .head   = mem_begin(scratchPages),
  };
  return (Allocator*)&g_allocatorIntern;
}

void alloc_scratch_teardown() { alloc_free(g_alloc_page, g_allocatorIntern.memory); }
