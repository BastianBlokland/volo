#include "alloc_internal.h"
#include "core_diag.h"
#include <stdlib.h>

static Mem alloc_heap_alloc(Allocator* allocator, const usize size) {
  (void)allocator;

  diag_assert(size);
  return (Mem){
      .ptr  = malloc(size),
      .size = size,
  };
}

static void alloc_heap_free(Allocator* allocator, Mem mem) {
  (void)allocator;

  diag_assert(mem_valid(mem));
  mem_set(mem, 0xFF); // Basic tag to detect use-after-free.
  free(mem.ptr);
}

Allocator g_allocatorHeap = {
    &alloc_heap_alloc,
    &alloc_heap_free,
};
