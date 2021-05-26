#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynbitset.h"

static void test_dynbitset_new_is_empty() {
  Allocator* alloc = alloc_bump_create_stack(128);

  DynBitSet bitset = dynbitset_create(alloc, 8);
  diag_assert(dynbitset_size(&bitset) == 0);
  dynbitset_destroy(&bitset);
}

static void test_dynbitset_size_is_a_multiple_of_8() {
  Allocator* alloc = alloc_bump_create_stack(128);

  DynBitSet bitset = dynbitset_create(alloc, 8);
  dynbitset_set(&bitset, 1);
  diag_assert(dynbitset_size(&bitset) == 8);
  dynbitset_set(&bitset, 42);
  diag_assert(dynbitset_size(&bitset) == 48);
  dynbitset_destroy(&bitset);
}

static void test_dynbitset_or() {
  Allocator* alloc = alloc_bump_create_stack(128);

  DynBitSet bitset = dynbitset_create(alloc, 8);

  const BitSet otherBits = bitset_from_var((u32){0b01000100010001000100010010000000});
  dynbitset_or(&bitset, otherBits);
  diag_assert(bitset_all_of(dynbitset_view(&bitset), otherBits));

  dynbitset_destroy(&bitset);
}

void test_dynbitset() {
  test_dynbitset_new_is_empty();
  test_dynbitset_size_is_a_multiple_of_8();
  test_dynbitset_or();
}
