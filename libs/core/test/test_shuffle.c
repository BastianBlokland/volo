#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_shuffle.h"

spec(shuffle) {

  it("can shuffle values using the fisheryates algorithm") {
    static const u64 seed = 42;

    Allocator* alloc = alloc_bump_create_stack(256);
    Rng*       rng   = rng_create_xorwow(alloc, seed);

    i32 ints[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    shuffle_fisheryates_t(rng, ints, ints + array_elems(ints), i32);

    i32 expectedInts[] = {0, 2, 9, 7, 1, 8, 5, 4, 3, 6};

    for (i32 i = 0; i != array_elems(ints); ++i) {
      check_eq_int(ints[i], expectedInts[i]);
    }

    rng_destroy(rng);
  }
}
