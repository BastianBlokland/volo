#include "core_alloc.h"
#include "core_diag.h"

static void test_alloc_page_alignment_matches_page_size() {
  const usize pageSize = alloc_min_size(g_alloc_page);

  Mem alloc = alloc_alloc(g_alloc_page, 8, 2);
  diag_assert(((uptr)alloc.ptr & (pageSize - 1)) == 0);

  alloc_free(g_alloc_page, alloc);
}

static void test_alloc_page_smaller_then_pagesize_can_be_allocated() {
  const Mem alloc = alloc_alloc(g_alloc_page, 64, 8);

  diag_assert(alloc.size == 64);

  alloc_free(g_alloc_page, alloc);
}

void test_alloc_page() {
  test_alloc_page_alignment_matches_page_size();
  test_alloc_page_smaller_then_pagesize_can_be_allocated();
}
