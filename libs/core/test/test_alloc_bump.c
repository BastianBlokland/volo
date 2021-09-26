#include "check_spec.h"
#include "core_alloc.h"

spec(alloc_bump) {

  it("tracks the maximum remaining size") {
    Allocator* alloc = alloc_bump_create_stack(128);

    // Starting 'maxSize' is not the same as the memory size as the bump allocator itself needs
    // space for its bookkeeping.
    const usize startingSize = alloc_max_size(alloc);

    alloc_alloc(alloc, 32, sizeof(void*));

    check_eq_int(alloc_max_size(alloc), startingSize - 32);

    alloc_alloc(alloc, alloc_max_size(alloc), sizeof(void*));
    check_eq_int(alloc_max_size(alloc), 0);
  }

  it("respects the requested alignment") {
    Allocator* alloc = alloc_bump_create_stack(256);
    alloc_alloc(alloc, 32, 32); // Start with an alignment of (atleast) 32 bytes.

    const usize startingSize = alloc_max_size(alloc);

    alloc_alloc(alloc, 6, 1);

    check_eq_int(alloc_max_size(alloc), startingSize - 6);

    alloc_alloc(alloc, 8, 8);

    check_eq_int(alloc_max_size(alloc), startingSize - 16);

    alloc_alloc(alloc, 64, 32);

    check_eq_int(alloc_max_size(alloc), startingSize - 96);
  }

  it("can be reset") {
    Allocator* alloc = alloc_bump_create_stack(256);

    Mem memA = alloc_alloc(alloc, 150, 1);
    check(mem_valid(memA));

    // Second allocation fails as the allocator is out of space.
    Mem memB = alloc_alloc(alloc, 150, 1);
    check(!mem_valid(memB));

    // Reset the allocator.
    alloc_reset(alloc);

    // Allocation now succeeds again and returns the same memory as the first call.
    Mem memC = alloc_alloc(alloc, 150, 1);
    check(mem_valid(memC));
    check(memA.ptr == memC.ptr);
  }
}
