#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"

static void test_dynarray_new_array_is_empty() {
  alloc_bump_create_stack(alloc, 128);

  DynArray array = dynarray_create_t(alloc, u64, 8);
  diag_assert(array.stride == sizeof(u64));
  diag_assert(array.size == 0);
  dynarray_destroy(&array);
}

static void test_dynarray_initial_capacity_can_be_zero() {
  alloc_bump_create_stack(alloc, 128);

  DynArray array = dynarray_create_t(alloc, u64, 0);
  diag_assert(!mem_valid(array.data));

  dynarray_push(&array, 1);
  diag_assert(mem_valid(array.data));

  dynarray_destroy(&array);
}

static void test_dynarray_resizing_changes_size() {
  alloc_bump_create_stack(alloc, 1024);

  DynArray array = dynarray_create_t(alloc, u64, 8);

  dynarray_resize(&array, 0);
  diag_assert(array.size == 0);

  dynarray_resize(&array, 1);
  diag_assert(array.size == 1);

  dynarray_resize(&array, 33);
  diag_assert(array.size == 33);

  dynarray_destroy(&array);
}

static void test_dynarray_resizing_preserves_content() {
  alloc_bump_create_stack(alloc, 1024);

  const u32 entries = 33;

  DynArray array = dynarray_create_t(alloc, u64, 8);

  for (u32 i = 0; i != entries; ++i) {
    *dynarray_push_t(&array, u64) = i;
  }

  dynarray_resize(&array, 64);

  for (u32 i = 0; i != entries; ++i) {
    diag_assert(*dynarray_at_t(&array, i, u64) == i);
  }

  dynarray_destroy(&array);
}

static void test_dynarray_pushing_increases_size() {
  alloc_bump_create_stack(alloc, 1024);

  const u32 amountToPush = 33;

  DynArray array = dynarray_create_t(alloc, u64, 8);
  for (u32 i = 0; i != amountToPush; ++i) {
    dynarray_push(&array, 1);
    diag_assert(array.size == i + 1);
  }
  dynarray_destroy(&array);
}

static void test_dynarray_popping_decreases_size() {
  alloc_bump_create_stack(alloc, 1024);

  const u32 startingSize = 33;

  DynArray array = dynarray_create_t(alloc, u64, 8);
  dynarray_resize(&array, startingSize);

  for (u32 i = startingSize; i-- > 0;) {
    dynarray_pop(&array, 1);
    diag_assert(array.size == i);
  }
  dynarray_destroy(&array);
}

static void test_dynarray_remove(const u32 removeIdx, const u32 removeCount) {
  alloc_bump_create_stack(alloc, 512);

  DynArray array = dynarray_create_t(alloc, u64, 8);

  const u64 values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  mem_cpy(dynarray_push(&array, array_elems(values)), array_mem(values));

  dynarray_remove(&array, removeIdx, removeCount);
  diag_assert(array.size == array_elems(values) - removeCount);

  dynarray_for_t(&array, u64, val, {
    if (val_i < removeIdx)
      diag_assert(*val == values[val_i]);
    else
      diag_assert(*val == values[removeCount + val_i]);
  });

  dynarray_destroy(&array);
}

static void test_dynarray_remove_unordered(
    const u16*  initial,
    const usize initialSize,
    const u32   removeIdx,
    const u32   removeCount,
    const u16*  expected) {
  alloc_bump_create_stack(alloc, 128);

  DynArray array = dynarray_create_t(alloc, u16, 8);
  mem_cpy(dynarray_push(&array, initialSize), mem_create(initial, sizeof(u16) * initialSize));

  dynarray_remove_unordered(&array, removeIdx, removeCount);
  diag_assert(array.size == initialSize - removeCount);

  dynarray_for_t(&array, u16, val, { diag_assert(*val == expected[val_i]); });

  dynarray_destroy(&array);
}

static void test_dynarray_insert(const u32 insertIdx, const u32 insertCount) {
  alloc_bump_create_stack(alloc, 512);

  DynArray array = dynarray_create_t(alloc, u32, 8);

  const u32 values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  mem_cpy(dynarray_push(&array, array_elems(values)), array_mem(values));

  mem_set(dynarray_insert(&array, insertIdx, insertCount), 0xBB);
  diag_assert(array.size == array_elems(values) + insertCount);

  dynarray_for_t(&array, u32, val, {
    if (val_i < insertIdx)
      diag_assert(*val == values[val_i]);
    else if (val_i < insertIdx + insertCount)
      diag_assert(*val == 0xBBBBBBBB);
    else
      diag_assert(*val == values[val_i - insertCount]);
  });

  dynarray_destroy(&array);
}

void test_dynarray() {
  test_dynarray_initial_capacity_can_be_zero();
  test_dynarray_new_array_is_empty();
  test_dynarray_resizing_changes_size();
  test_dynarray_resizing_preserves_content();
  test_dynarray_pushing_increases_size();
  test_dynarray_popping_decreases_size();

  test_dynarray_remove(0, 3);
  test_dynarray_remove(1, 3);
  test_dynarray_remove(5, 3);
  test_dynarray_remove(7, 3);
  test_dynarray_remove(9, 1);
  test_dynarray_remove(0, 10);

  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5}, 5, 0, 1, (u16[]){5, 2, 3, 4});
  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5}, 5, 1, 1, (u16[]){1, 5, 3, 4});
  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5}, 5, 0, 2, (u16[]){4, 5, 3});
  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5}, 5, 0, 3, (u16[]){4, 5});
  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5}, 5, 0, 4, (u16[]){5});
  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5}, 5, 0, 5, null);
  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5, 6}, 6, 2, 1, (u16[]){1, 2, 6, 4, 5});
  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5, 6}, 6, 2, 2, (u16[]){1, 2, 5, 6, 4});
  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5, 6}, 6, 5, 1, (u16[]){1, 2, 3, 4, 5});
  test_dynarray_remove_unordered((u16[]){1, 2, 3, 4, 5, 6}, 6, 4, 2, (u16[]){1, 2, 3, 4});

  test_dynarray_insert(0, 3);
  test_dynarray_insert(1, 3);
  test_dynarray_insert(5, 5);
  test_dynarray_insert(10, 10);
}
