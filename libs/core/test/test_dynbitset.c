#include "core_alloc.h"
#include "core_dynbitset.h"

#include "check_spec.h"

spec(dynbitset) {

  it("can create an empty Dynamic-BitSet") {
    Allocator* alloc = alloc_bump_create_stack(128);

    DynBitSet bitset = dynbitset_create(alloc, 8);
    check_eq_u64(dynbitset_size(&bitset), 0);
    dynbitset_destroy(&bitset);
  }

  it("always has a size that is a multiple of 8") {
    Allocator* alloc = alloc_bump_create_stack(128);

    DynBitSet bitset = dynbitset_create(alloc, 8);
    dynbitset_set(&bitset, 1);
    check_eq_u64(dynbitset_size(&bitset), 8);
    dynbitset_set(&bitset, 42);
    check_eq_u64(dynbitset_size(&bitset), 48);
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
