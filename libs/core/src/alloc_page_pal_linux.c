#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"

#include "alloc_internal.h"

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

/**
 * Platform page allocator.
 * NOTE: Do NOT add locks (neither mutex nor spin-lock) to the page-allocation path as then it will
 * become unsafe to be called after fork (as another thread might have held the lock).
 */

typedef struct {
  Allocator api;
  usize     pageSize;
  i64       allocatedPages;
  i64       counter; // Incremented on every allocation.
} AllocatorPage;

static u32 alloc_page_num_pages(AllocatorPage* allocPage, const usize size) {
  return (u32)((size + allocPage->pageSize - 1) / allocPage->pageSize);
}

static Mem alloc_page_alloc(Allocator* allocator, const usize size, const usize align) {
  AllocatorPage* allocPage = (AllocatorPage*)allocator;
  (void)align;

  diag_assert_msg(
      bits_aligned(allocPage->pageSize, align),
      "alloc_page_alloc: Alignment '{}' cannot be satisfied (stronger then pageSize alignment)",
      fmt_int(align));

  const u32   pages    = alloc_page_num_pages(allocPage, size);
  const usize realSize = pages * allocPage->pageSize;

  void* res = mmap(null, realSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (UNLIKELY(res == MAP_FAILED)) {
    return mem_create(null, size);
  }

  thread_atomic_add_i64(&allocPage->allocatedPages, pages);
  thread_atomic_add_i64(&allocPage->counter, 1);
  return mem_create(res, size);
}

static void alloc_page_free(Allocator* allocator, Mem mem) {
  diag_assert(mem_valid(mem));

  AllocatorPage* allocPage = (AllocatorPage*)allocator;

  const u32 pages = alloc_page_num_pages(allocPage, mem.size);
  const int res   = munmap(mem.ptr, pages * allocPage->pageSize);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("munmap() failed: {} (errno: {})", fmt_int(res), fmt_int(errno));
  }
  thread_atomic_sub_i64(&allocPage->allocatedPages, pages);
}

static usize alloc_page_max_size(Allocator* allocator) {
  (void)allocator;
  return alloc_max_alloc_size;
}

static AllocatorPage g_allocatorIntern;

Allocator* alloc_page_init(void) {
  const size_t pageSize = getpagesize();
  g_allocatorIntern     = (AllocatorPage){
      .api =
          {
              .alloc   = alloc_page_alloc,
              .free    = alloc_page_free,
              .maxSize = alloc_page_max_size,
              .reset   = null,
          },
      .pageSize = pageSize,
  };
  return (Allocator*)&g_allocatorIntern;
}

u32 alloc_page_allocated_pages(void) {
  return (u32)thread_atomic_load_i64(&g_allocatorIntern.allocatedPages);
}

usize alloc_page_allocated_size(void) {
  return alloc_page_allocated_pages() * g_allocatorIntern.pageSize;
}

u64 alloc_page_counter(void) { return (u64)thread_atomic_load_i64(&g_allocatorIntern.counter); }
