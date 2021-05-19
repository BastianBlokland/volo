#include "alloc_internal.h"
#include "core_diag.h"

Mem alloc_alloc(Allocator* allocator, usize size) {
  diag_assert(allocator);
  return allocator->alloc(allocator, size);
}

void alloc_free(Allocator* allocator, Mem mem) {
  diag_assert(allocator);
  return allocator->free(allocator, mem);
}
