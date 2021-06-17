#include "core_alloc.h"
#include "core_diag.h"

static void test_alloc_bump_max_size() {
  Allocator* alloc = alloc_bump_create_stack(128);

  // Starting 'maxSize' is not the same as the memory size as the bump allocator itself needs space
  // for its bookkeeping.
  const usize startingSize = alloc_max_size(alloc);

  alloc_alloc(alloc, 32, sizeof(void*));

  diag_assert(alloc_max_size(alloc) == startingSize - 32);

  alloc_alloc(alloc, alloc_max_size(alloc), sizeof(void*));
  diag_assert(alloc_max_size(alloc) == 0);
}

static void test_alloc_bump_alignment() {
  Allocator* alloc = alloc_bump_create_stack(256);
  alloc_alloc(alloc, 32, 32); // Start with an alignment of (atleast) 32 bytes.

  const usize startingSize = alloc_max_size(alloc);

  alloc_alloc(alloc, 6, 1);

  diag_assert(alloc_max_size(alloc) == startingSize - 6);

  alloc_alloc(alloc, 8, 8);

  diag_assert(alloc_max_size(alloc) == startingSize - 16);

  alloc_alloc(alloc, 64, 32);

  diag_assert(alloc_max_size(alloc) == startingSize - 96);
}

void test_alloc_bump() {
  test_alloc_bump_max_size();
  test_alloc_bump_alignment();
}
