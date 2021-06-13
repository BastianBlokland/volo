#include "alloc_internal.h"
#include "core_diag.h"
#include "init_internal.h"

Allocator*   g_alloc_heap;
Allocator*   g_alloc_page;
THREAD_LOCAL Allocator* g_alloc_scratch;

void alloc_init() {
  g_alloc_heap = alloc_heap_init();
  g_alloc_page = alloc_page_init();
}

void alloc_init_thread() { g_alloc_scratch = alloc_scratch_init(); }

void alloc_teardown_thread() {
  alloc_scratch_teardown();
  g_alloc_scratch = null;
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

usize alloc_max_size(Allocator* allocator) {
  diag_assert_msg(allocator, string_lit("Allocator is not initialized"));
  return allocator->maxSize(allocator);
}
