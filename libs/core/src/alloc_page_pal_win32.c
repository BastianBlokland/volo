#include "alloc_internal.h"
#include "core_bits.h"
#include "core_diag.h"
#include <Windows.h>

struct AllocatorPage {
  Allocator api;
  usize     pageSize;
};

static Mem alloc_page_alloc(Allocator* allocator, const usize size) {
  diag_assert(size);

  const usize pageSize    = ((struct AllocatorPage*)allocator)->pageSize;
  const usize alignedSize = bits_align_64(size, pageSize);

  void* ptr = VirtualAlloc(null, alignedSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  return mem_create(ptr, alignedSize);
}

static void alloc_page_free(Allocator* allocator, Mem mem) {
  (void)allocator;

  diag_assert(mem_valid(mem));

  const bool success = VirtualFree(mem.ptr, 0, MEM_RELEASE) == TRUE;
  diag_assert_msg(success, "VirtualFree() failed");
  (void)success;
}

static usize alloc_heap_min_size(Allocator* allocator) {
  return ((struct AllocatorPage*)allocator)->pageSize;
}

static struct AllocatorPage g_allocatorIntern;

Allocator* alloc_page_init() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const size_t pageSize = si.dwPageSize;

  g_allocatorIntern = (struct AllocatorPage){
      (Allocator){
          &alloc_page_alloc,
          &alloc_page_free,
          &alloc_heap_min_size,
      },
      pageSize,
  };
  return (Allocator*)&g_allocatorIntern;
}
