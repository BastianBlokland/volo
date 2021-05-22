#include "alloc_internal.h"
#include "core_diag.h"

static bool g_intialized = false;

Allocator* g_allocatorHeap;
Allocator* g_allocatorPage;

void alloc_init() {
  if (!g_intialized) {
    g_allocatorHeap = alloc_init_heap();
    g_allocatorPage = alloc_init_page();
  }
  g_intialized = true;
}

Mem alloc_alloc(Allocator* allocator, usize size) {
  diag_assert_msg(allocator, "Allocator is not initialized");
  return allocator->alloc(allocator, size);
}

void alloc_free(Allocator* allocator, Mem mem) {
  diag_assert_msg(allocator, "Allocator is not initialized");
  return allocator->free(allocator, mem);
}
