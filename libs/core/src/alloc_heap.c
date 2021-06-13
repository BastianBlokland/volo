#include "alloc_internal.h"
#include "core_diag.h"

#define freed_mem_tag 0xFF

// Forward declare libc malloc(size_t) and free(void*).
void* malloc(size_t);
void  free(void*);

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
  mem_set(mem, freed_mem_tag); // Tag to detect use-after-free.
  free(mem.ptr);
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
      },
  };
  return (Allocator*)&g_allocatorIntern;
}
