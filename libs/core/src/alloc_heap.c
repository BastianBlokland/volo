#include "core_diag.h"

#include "alloc_internal.h"

#include <stdlib.h>

#define freed_mem_tag 0xFA

struct AllocatorHeap {
  Allocator api;
};

static Mem alloc_heap_alloc(Allocator* allocator, const usize size, const usize align) {
  (void)allocator;

#if defined(VOLO_LINUX)
  return mem_create(aligned_alloc(align, size), size);
#elif defined(VOLO_WIN32)
  return mem_create(_aligned_malloc(size, align), size);
#else
  ASSERT(false, "Unsupported platform");
#endif
}

static void alloc_heap_free(Allocator* allocator, Mem mem) {
  (void)allocator;

  diag_assert(mem_valid(mem));
  mem_set(mem, freed_mem_tag); // Tag to detect use-after-free.

#if defined(VOLO_LINUX)
  free(mem.ptr);
#elif defined(VOLO_WIN32)
  _aligned_free(mem.ptr);
#else
  ASSERT(false, "Unsupported platform");
#endif
}

static usize alloc_heap_min_size(Allocator* allocator) {
  (void)allocator;
  return 1;
}

static usize alloc_heap_max_size(Allocator* allocator) {
  (void)allocator;
  return usize_max;
}

static struct AllocatorHeap g_allocatorIntern;

Allocator* alloc_heap_init() {
  g_allocatorIntern = (struct AllocatorHeap){
      (Allocator){
          .alloc   = alloc_heap_alloc,
          .free    = alloc_heap_free,
          .minSize = alloc_heap_min_size,
          .maxSize = alloc_heap_max_size,
          .reset   = null,
      },
  };
  return (Allocator*)&g_allocatorIntern;
}
