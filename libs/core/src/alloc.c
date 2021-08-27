#include "core_diag.h"

#include "alloc_internal.h"
#include "init_internal.h"

#define alloc_max_alloc_size (usize_mebibyte * 128)

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

INLINE_HINT Mem alloc_alloc(Allocator* allocator, const usize size, const usize align) {
  diag_assert_msg(allocator, "alloc_alloc: Allocator is not initialized");
  diag_assert_msg(size, "alloc_alloc: 0 byte allocations are not valid");
  diag_assert_msg(
      bits_ispow2(align), "alloc_alloc: Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      (size & (align - 1)) == 0,
      "alloc_alloc: Size '{}' is not a multiple of the alignment '{}'",
      fmt_size(size),
      fmt_int(align));
  diag_assert_msg(
      size < alloc_max_alloc_size,
      "alloc_alloc: Size '{}' is bigger then the maximum of '{}'",
      fmt_size(size),
      fmt_size(alloc_max_alloc_size));

  return allocator->alloc(allocator, size, align);
}

INLINE_HINT void alloc_free(Allocator* allocator, Mem mem) {
  diag_assert_msg(allocator, "alloc_free: Allocator is not initialized");
  allocator->free(allocator, mem);
}

INLINE_HINT usize alloc_min_size(Allocator* allocator) {
  diag_assert_msg(allocator, "alloc_min_size: Allocator is not initialized");
  return allocator->minSize(allocator);
}

INLINE_HINT usize alloc_max_size(Allocator* allocator) {
  diag_assert_msg(allocator, "alloc_max_size: Allocator is not initialized");
  return allocator->maxSize(allocator);
}
