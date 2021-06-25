#include "core_bits.h"
#include "core_diag.h"

#include <sys/mman.h>
#include <unistd.h>

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

  void* res = mmap(null, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return mem_create(res == MAP_FAILED ? null : res, size);
}

static void alloc_page_free(Allocator* allocator, Mem mem) {
  (void)allocator;

  diag_assert(mem_valid(mem));

  const int res = munmap(mem.ptr, mem.size);
  diag_assert_msg(res == 0, "munmap() failed: {}", fmt_int(res));
  (void)res;
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
  const size_t pageSize = getpagesize();
  g_allocatorIntern     = (struct AllocatorPage){
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
