#include "core_bits.h"
#include "core_diag.h"

#include <Windows.h>

#include "alloc_internal.h"

struct AllocatorPage {
  Allocator api;
  usize     pageSize;
};

static Mem alloc_page_alloc(Allocator* allocator, const usize size, const usize align) {
  const usize pageSize = ((struct AllocatorPage*)allocator)->pageSize;
  diag_assert_msg(
      (pageSize & (align - 1)) == 0,
      "alloc_page_alloc: Alignment '{}' cannot be satisfied (stronger then pageSize alignment)",
      fmt_int(align));
  (void)pageSize;

  void* ptr = VirtualAlloc(null, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  return mem_create(ptr, size);
}

static void alloc_page_free(Allocator* allocator, Mem mem) {
  (void)allocator;

  diag_assert(mem_valid(mem));

  const bool success = VirtualFree(mem.ptr, 0, MEM_RELEASE) == TRUE;
  diag_assert_msg(success, "VirtualFree() failed");
  (void)success;
}

static usize alloc_page_min_size(Allocator* allocator) {
  return ((struct AllocatorPage*)allocator)->pageSize;
}

static usize alloc_page_max_size(Allocator* allocator) {
  (void)allocator;
  return usize_max;
}

static struct AllocatorPage g_allocatorIntern;

Allocator* alloc_page_init() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const size_t pageSize = si.dwPageSize;

  g_allocatorIntern = (struct AllocatorPage){
      (Allocator){
          .alloc   = alloc_page_alloc,
          .free    = alloc_page_free,
          .minSize = alloc_page_min_size,
          .maxSize = alloc_page_max_size,
      },
      pageSize,
  };
  return (Allocator*)&g_allocatorIntern;
}
