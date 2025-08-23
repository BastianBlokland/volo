#include "core/alloc.h"
#include "core/diag.h"

#include "alloc.h"

#define scratch_heap_size (usize_mebibyte * 2)
#define scratch_max_alloc_size (usize_kibibyte * 256)
#define scratch_guard_enable 0
#define scratch_guard_size (usize_kibibyte * 512)

typedef struct {
  Allocator api;
  Mem       memory;
  u8*       head;
} AllocatorScratch;

/**
 * Pre-condition: bits_ispow2(_ALIGN_)
 */
INLINE_HINT static u8* alloc_scratch_align_ptr(u8* ptr, const usize align) {
  return (u8*)((uptr)ptr + ((~(uptr)ptr + 1) & (align - 1)));
}

/**
 * Tag a fixed-size region in-front of the scratch write head. This aids in detecting when the
 * application holds onto scratch memory for too long (and thus is about to be overwritten).
 */
MAYBE_UNUSED static void alloc_scratch_tag_guard(AllocatorScratch* allocScratch, const usize size) {
  const usize memUntilEnd = mem_end(allocScratch->memory) - allocScratch->head;
  if (memUntilEnd > size) {
    alloc_tag_guard(mem_create(allocScratch->head, size), AllocMemType_Scratch);
  } else {
    alloc_tag_guard(mem_create(allocScratch->head, memUntilEnd), AllocMemType_Scratch);
    alloc_tag_guard(mem_create(mem_begin(allocScratch->memory), size), AllocMemType_Scratch);
  }
}

static Mem alloc_scratch_alloc(Allocator* allocator, const usize size, const usize align) {
  AllocatorScratch* allocScratch = (AllocatorScratch*)allocator;

  if (UNLIKELY(size > scratch_max_alloc_size)) {
    // Too big allocation, we limit the maximum allocation size to avoid 'invalidating' too many
    // other scratch allocations at once.
    return mem_create(null, size);
  }

  u8* alignedHead = alloc_scratch_align_ptr(allocScratch->head, align);

  if (UNLIKELY(alignedHead + size > mem_end(allocScratch->memory))) {
    // Wrap around the scratch buffer.
    alignedHead = alloc_scratch_align_ptr(mem_begin(allocScratch->memory), align);
  }

  allocScratch->head = alignedHead + size;

#if scratch_guard_enable
  alloc_scratch_tag_guard(allocScratch, scratch_guard_size);
#endif

  return mem_create(alignedHead, size);
}

static void alloc_scratch_free(Allocator* allocator, Mem mem) {
  (void)allocator;
  (void)mem;

  diag_assert(mem_valid(mem));

  // NOTE: Tag the freed memory to detect UAF.
  alloc_tag_free(mem, AllocMemType_Scratch);
}

static usize alloc_scratch_max_size(Allocator* allocator) {
  (void)allocator;
  return scratch_max_alloc_size;
}

static THREAD_LOCAL AllocatorScratch g_allocatorIntern;

Allocator* alloc_scratch_init(void) {
  Mem scratchPages  = alloc_alloc(g_allocPage, scratch_heap_size, sizeof(void*));
  g_allocatorIntern = (AllocatorScratch){
      (Allocator){
          .alloc   = alloc_scratch_alloc,
          .free    = alloc_scratch_free,
          .maxSize = alloc_scratch_max_size,
          .reset   = null,
      },
      .memory = scratchPages,
      .head   = mem_begin(scratchPages),
  };
  return (Allocator*)&g_allocatorIntern;
}

void alloc_scratch_teardown(void) {
  alloc_free(g_allocPage, g_allocatorIntern.memory);
  g_allocatorIntern = (AllocatorScratch){0};
}
