#include "core_bits.h"
#include "core_math.h"
#include "core_thread.h"

#include "alloc_internal.h"

#include <Windows.h>

/**
 * Platform page allocator.
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

#ifndef VOLO_FAST
  if (UNLIKELY(!bits_aligned(allocPage->pageSize, align))) {
    alloc_crash_with_msg(
        "alloc_page_alloc: Alignment '{}' invalid (stronger then pageSize)", fmt_int(align));
  }
#endif

  const u32   pages    = alloc_page_num_pages(allocPage, size);
  const usize realSize = pages * allocPage->pageSize;

  void* ptr = VirtualAlloc(null, realSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (UNLIKELY(!ptr)) {
    return mem_create(null, size);
  }

  thread_atomic_add_i64(&allocPage->allocatedPages, pages);
  thread_atomic_add_i64(&allocPage->counter, 1);
  return mem_create(ptr, size);
}

static void alloc_page_free(Allocator* allocator, Mem mem) {
#ifndef VOLO_FAST
  if (UNLIKELY(!mem_valid(mem))) {
    alloc_crash_with_msg("alloc_page_alloc: Invalid allocation");
  }
#endif

  AllocatorPage* allocPage = (AllocatorPage*)allocator;

  const u32 pages = alloc_page_num_pages(allocPage, mem.size);

  const int success = VirtualFree(mem.ptr, 0, MEM_RELEASE);
  if (UNLIKELY(!success)) {
    alloc_crash_with_msg("VirtualFree() failed");
  }
  thread_atomic_sub_i64(&allocPage->allocatedPages, pages);
}

static usize alloc_page_max_size(Allocator* allocator) {
  (void)allocator;
  return alloc_max_alloc_size;
}

static AllocatorPage g_allocatorIntern;

Allocator* alloc_page_init(void) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const size_t pageSize = si.dwPageSize;
  if (UNLIKELY(!bits_ispow2(pageSize))) {
    alloc_crash_with_msg("Non pow2 page-size is not supported");
  }

  g_allocatorIntern = (AllocatorPage){
      .api =
          (Allocator){
              .alloc   = alloc_page_alloc,
              .free    = alloc_page_free,
              .maxSize = alloc_page_max_size,
              .reset   = null,
          },
      .pageSize = pageSize,
  };
  return (Allocator*)&g_allocatorIntern;
}

usize alloc_page_size(void) { return g_allocatorIntern.pageSize; }

u32 alloc_page_allocated_pages(void) {
  return (u32)thread_atomic_load_i64(&g_allocatorIntern.allocatedPages);
}

usize alloc_page_allocated_size(void) {
  return alloc_page_allocated_pages() * g_allocatorIntern.pageSize;
}

u64 alloc_page_counter(void) { return (u64)thread_atomic_load_i64(&g_allocatorIntern.counter); }
