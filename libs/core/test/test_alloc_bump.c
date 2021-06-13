#include "core_alloc.h"
#include "core_diag.h"

static void test_alloc_bump_max_size() {
  Allocator* alloc = alloc_bump_create_stack(128);

  // Starting 'maxSize' is not the same as the memory size as the bump allocator itself needs space
  // for its bookkeeping.
  const usize startingSize = alloc_max_size(alloc);

  alloc_alloc(alloc, 20);

  diag_assert(alloc_max_size(alloc) == startingSize - 20);

  alloc_alloc(alloc, alloc_max_size(alloc));
  diag_assert(alloc_max_size(alloc) == 0);
}

void test_alloc_bump() { test_alloc_bump_max_size(); }
