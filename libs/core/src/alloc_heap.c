#include "alloc_internal.h"
#include "core_diag.h"
#include <stdlib.h>

struct AllocatorHeap {
  Allocator api;
};

static Mem alloc_heap_alloc(Allocator* allocator, const usize size) {
  (void)allocator;

  diag_assert(size);
  return mem_create(malloc(size), size);
}

static void alloc_heap_free(Allocator* allocator, Mem mem) {
  (void)allocator;

  diag_assert(mem_valid(mem));
  mem_set(mem, 0xFF); // Basic tag to detect use-after-free.
  free(mem.ptr);
}

static usize alloc_heap_min_size(Allocator* allocator) {
  (void)allocator;
  return 1;
}

static struct AllocatorHeap g_allocatorIntern;

Allocator* alloc_heap_init() {
  g_allocatorIntern = (struct AllocatorHeap){
      (Allocator){
          &alloc_heap_alloc,
          &alloc_heap_free,
          &alloc_heap_min_size,
      },
  };
  return (Allocator*)&g_allocatorIntern;
}
