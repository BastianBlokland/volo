#include "check_spec.h"
#include "core_alloc.h"
#include "core_dynbitset.h"

spec(dynbitset) {

  it("can create an empty Dynamic-BitSet") {
    Allocator* alloc = alloc_bump_create_stack(128);

    DynBitSet bitset = dynbitset_create(alloc, 8);
    check_eq_int(dynbitset_size(&bitset), 0);
    dynbitset_destroy(&bitset);
  }

  it("always has a size that is a multiple of 8") {
    Allocator* alloc = alloc_bump_create_stack(128);

    DynBitSet bitset = dynbitset_create(alloc, 8);
    dynbitset_set(&bitset, 1);
    check_eq_int(dynbitset_size(&bitset), 8);
    dynbitset_set(&bitset, 42);
    check_eq_int(dynbitset_size(&bitset), 48);
    dynbitset_destroy(&bitset);
  }

  it("can test for set bits") {
    Allocator* alloc  = alloc_bump_create_stack(128);
    DynBitSet  bitset = dynbitset_create(alloc, 0);

    check(!dynbitset_test(&bitset, 0));
    check(!dynbitset_test(&bitset, 1));
    check(!dynbitset_test(&bitset, 42));

    dynbitset_set(&bitset, 42);
    check_eq_int(dynbitset_size(&bitset), 48); // Nearest 8 bit aligned size.

    check(dynbitset_test(&bitset, 42));
    check(!dynbitset_test(&bitset, 41));
    check(!dynbitset_test(&bitset, 48));
    check(!dynbitset_test(&bitset, 49));

    dynbitset_clear(&bitset, 42);
    check(!dynbitset_test(&bitset, 42));

    dynbitset_destroy(&bitset);
  }

  it("automatically allocates space when performing a set_all") {
    Allocator* alloc  = alloc_bump_create_stack(128);
    DynBitSet  bitset = dynbitset_create(alloc, 0);

    dynbitset_set_all(&bitset, 42);
    for (usize i = 0; i != 42; ++i) {
      check(bitset_test(dynbitset_view(&bitset), i));
    }
    check(!bitset_test(dynbitset_view(&bitset), 42));

    dynbitset_destroy(&bitset);
  }

  it("automatically allocates space when performing a bitwise 'or'") {
    Allocator* alloc = alloc_bump_create_stack(128);

    DynBitSet bitset = dynbitset_create(alloc, 8);

    const BitSet otherBits = bitset_from_var((u32){0b01000100010001000100010010000000});
    dynbitset_or(&bitset, otherBits);
    check(bitset_all_of(dynbitset_view(&bitset), otherBits));

    dynbitset_destroy(&bitset);
  }
}
