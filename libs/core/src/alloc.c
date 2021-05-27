#include "alloc_internal.h"
#include "core_diag.h"

Allocator* g_allocatorHeap;
Allocator* g_allocatorPage;

void alloc_init() {
  g_allocatorHeap = alloc_heap_init();
  g_allocatorPage = alloc_page_init();
}

Mem alloc_alloc(Allocator* allocator, const usize size) {
  diag_assert_msg(allocator, "Allocator is not initialized");
  return allocator->alloc(allocator, size);
}

void alloc_free(Allocator* allocator, Mem mem) {
  diag_assert_msg(allocator, "Allocator is not initialized");
  allocator->free(allocator, mem);
}

usize alloc_min_size(Allocator* allocator) {
  diag_assert_msg(allocator, "Allocator is not initialized");
  return allocator->minSize(allocator);
}
