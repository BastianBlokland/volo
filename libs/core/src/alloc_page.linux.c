#include "alloc_internal.h"
#include "core_bits.h"
#include "core_diag.h"
#include <sys/mman.h>
#include <unistd.h>

struct AllocatorPage {
  Allocator api;
  usize     pageSize;
};

static Mem alloc_page_alloc(Allocator* allocator, const usize size) {
  diag_assert(size);

  const usize pageSize    = ((struct AllocatorPage*)allocator)->pageSize;
  const usize alignedSize = bits_align(size, pageSize);

  void* res = mmap(null, alignedSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return (Mem){
      .ptr  = res == MAP_FAILED ? null : res,
      .size = alignedSize,
  };
}

static void alloc_page_free(Allocator* allocator, Mem mem) {
  (void)allocator;

  diag_assert(mem_valid(mem));
  mem_set(mem, 0xFF); // Basic tag to detect use-after-free.

  int res = munmap(mem.ptr, mem.size);
  diag_assert_msg(res == 0, "munmap() failed");
  (void)res;
}

static usize alloc_heap_min_size(Allocator* allocator) {
  return ((struct AllocatorPage*)allocator)->pageSize;
}

static struct AllocatorPage g_allocatorIntern;

Allocator* alloc_init_page() {
  const size_t pageSize = getpagesize();
  g_allocatorIntern     = (struct AllocatorPage){
      (Allocator){
          &alloc_page_alloc,
          &alloc_page_free,
          &alloc_heap_min_size,
      },
      pageSize,
  };
  return (Allocator*)&g_allocatorIntern;
}
