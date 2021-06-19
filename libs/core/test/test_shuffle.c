#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_shuffle.h"

static void test_shuffle_fisheryates() {
  const static u64 seed = 42;

  Allocator* alloc = alloc_bump_create_stack(256);
  Rng*       rng   = rng_create_xorwow(alloc, seed);

  i32 ints[]  = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  Mem intsMem = array_mem(ints);

  shuffle_fisheryates(rng, mem_begin(intsMem), mem_end(intsMem), sizeof(i32));

  i32 expectedInts[] = {0, 2, 9, 7, 1, 8, 5, 4, 3, 6};

  for (i32 i = 0; i != array_elems(ints); ++i) {
    diag_assert(ints[i] == expectedInts[i]);
  }

  rng_destroy(rng);
}

void test_shuffle() { test_shuffle_fisheryates(); }
