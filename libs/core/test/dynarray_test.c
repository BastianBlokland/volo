#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"

static void test_dynarray_new_array_is_empty() {
  DynArray array = dynarray_init_t(u64, 8);
  diag_assert(array.stride == sizeof(u64));
  diag_assert(array.size == 0);
  dynarray_free(&array);
}

static void test_dynarray_initial_capacity_can_be_zero() {
  DynArray array = dynarray_init_t(u64, 0);
  diag_assert(!mem_valid(array.data));

  dynarray_push(&array, 1);
  diag_assert(mem_valid(array.data));

  dynarray_free(&array);
}

static void test_dynarray_resizing_changes_size() {
  DynArray array = dynarray_init_t(u64, 8);

  dynarray_resize(&array, 0);
  diag_assert(array.size == 0);

  dynarray_resize(&array, 1);
  diag_assert(array.size == 1);

  dynarray_resize(&array, 33);
  diag_assert(array.size == 33);

  dynarray_free(&array);
}

static void test_dynarray_resizing_preserves_content() {
  const u32 entries = 33;

  DynArray array = dynarray_init_t(u64, 8);

  for (u32 i = 0; i != entries; ++i) {
    *dynarray_push_t(&array, u64) = i;
  }

  dynarray_resize(&array, 64);

  for (u32 i = 0; i != entries; ++i) {
    diag_assert(*dynarray_at_t(&array, i, u64) == i);
  }

  dynarray_free(&array);
}

static void test_dynarray_pushing_increases_size() {
  const u32 amountToPush = 33;

  DynArray array = dynarray_init_t(u64, 8);
  for (u32 i = 0; i != amountToPush; ++i) {
    dynarray_push(&array, 1);
    diag_assert(array.size == i + 1);
  }
  dynarray_free(&array);
}

static void test_dynarray_popping_decreases_size() {
  const u32 startingSize = 33;

  DynArray array = dynarray_init_t(u64, 8);
  dynarray_resize(&array, startingSize);

  for (u32 i = startingSize; i-- > 0;) {
    dynarray_pop(&array, 1);
    diag_assert(array.size == i);
  }
  dynarray_free(&array);
}

static void test_dynarray_remove_shifts_content(const u32 removeIdx, const u32 removeCount) {

  DynArray array = dynarray_init_t(u64, 8);

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

  dynarray_free(&array);
}

static void test_dynarray_insert_shifts_content(const u32 insertIdx, const u32 insertCount) {

  DynArray array = dynarray_init_t(u32, 8);

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

  dynarray_free(&array);
}

void test_dynarray() {
  test_dynarray_initial_capacity_can_be_zero();
  test_dynarray_new_array_is_empty();
  test_dynarray_resizing_changes_size();
  test_dynarray_resizing_preserves_content();
  test_dynarray_pushing_increases_size();
  test_dynarray_popping_decreases_size();

  test_dynarray_remove_shifts_content(0, 3);
  test_dynarray_remove_shifts_content(1, 3);
  test_dynarray_remove_shifts_content(5, 3);
  test_dynarray_remove_shifts_content(7, 3);
  test_dynarray_remove_shifts_content(9, 1);
  test_dynarray_remove_shifts_content(0, 10);

  test_dynarray_insert_shifts_content(0, 3);
  test_dynarray_insert_shifts_content(1, 3);
  test_dynarray_insert_shifts_content(5, 5);
  test_dynarray_insert_shifts_content(10, 10);
}
