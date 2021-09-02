#include "core_alloc.h"

#include "check_spec.h"

spec(alloc_bump) {

  it("tracks the maximum remaining size") {
    Allocator* alloc = alloc_bump_create_stack(128);

    // Starting 'maxSize' is not the same as the memory size as the bump allocator itself needs
    // space for its bookkeeping.
    const usize startingSize = alloc_max_size(alloc);

    alloc_alloc(alloc, 32, sizeof(void*));

    check_eq_u64(alloc_max_size(alloc), startingSize - 32);

    alloc_alloc(alloc, alloc_max_size(alloc), sizeof(void*));
    check_eq_u64(alloc_max_size(alloc), 0);
  }

  it("respects the requested alignment") {
    Allocator* alloc = alloc_bump_create_stack(256);
    alloc_alloc(alloc, 32, 32); // Start with an alignment of (atleast) 32 bytes.

    const usize startingSize = alloc_max_size(alloc);

    alloc_alloc(alloc, 6, 1);

    check_eq_u64(alloc_max_size(alloc), startingSize - 6);

    alloc_alloc(alloc, 8, 8);

    check_eq_u64(alloc_max_size(alloc), startingSize - 16);

    alloc_alloc(alloc, 64, 32);

    check_eq_u64(alloc_max_size(alloc), startingSize - 96);
  }
}
