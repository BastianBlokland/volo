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
  const usize alignedSize = bits_align_64(size, pageSize);

  void* res = mmap(null, alignedSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return mem_create(res == MAP_FAILED ? null : res, alignedSize);
}

static void alloc_page_free(Allocator* allocator, Mem mem) {
  (void)allocator;

  diag_assert(mem_valid(mem));

  const int res = munmap(mem.ptr, mem.size);
  diag_assert_msg(res == 0, fmt_write_scratch("munmap() failed: {}", fmt_int(res)));
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
