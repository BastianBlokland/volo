#include "alloc_internal.h"
#include "core_diag.h"

Allocator* g_allocator_heap;
Allocator* g_allocator_page;

void alloc_init() {
  g_allocator_heap = alloc_heap_init();
  g_allocator_page = alloc_page_init();
}

Mem alloc_alloc(Allocator* allocator, const usize size) {
  diag_assert_msg(allocator, string_lit("Allocator is not initialized"));
  return allocator->alloc(allocator, size);
}

void alloc_free(Allocator* allocator, Mem mem) {
  diag_assert_msg(allocator, string_lit("Allocator is not initialized"));
  allocator->free(allocator, mem);
}

usize alloc_min_size(Allocator* allocator) {
  diag_assert_msg(allocator, string_lit("Allocator is not initialized"));
  return allocator->minSize(allocator);
}
