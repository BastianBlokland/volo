#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"

spec(dynarray) {

  it("can create a new empty Dynamic-Array") {
    Allocator* alloc = alloc_bump_create_stack(128);

    DynArray array = dynarray_create_t(alloc, u64, 8);
    check_eq_int(array.stride, sizeof(u64));
    check_eq_int(array.size, 0);
    dynarray_destroy(&array);
  }

  it("can create a Dynamic-Array with 0 capacity") {
    Allocator* alloc = alloc_bump_create_stack(128);

    DynArray array = dynarray_create_t(alloc, u64, 0);
    check(!mem_valid(array.data));

    dynarray_push(&array, 1);
    check(mem_valid(array.data));

    dynarray_destroy(&array);
  }

  it("can be resized") {
    Allocator* alloc = alloc_bump_create_stack(1024);

    DynArray array = dynarray_create_t(alloc, u64, 8);

    dynarray_resize(&array, 0);
    check_eq_int(array.size, 0);

    dynarray_resize(&array, 1);
    check_eq_int(array.size, 1);

    dynarray_resize(&array, 33);
    check_eq_int(array.size, 33);

    dynarray_destroy(&array);
  }

  it("preserves content while resizing") {
    Allocator* alloc = alloc_bump_create_stack(1024);

    const u32 entries = 33;

    DynArray array = dynarray_create_t(alloc, u64, 8);

    for (u32 i = 0; i != entries; ++i) {
      *dynarray_push_t(&array, u64) = i;
    }

    dynarray_resize(&array, 64);

    for (u32 i = 0; i != entries; ++i) {
      check_eq_int(*dynarray_at_t(&array, i, u64), i);
    }

    dynarray_destroy(&array);
  }

  it("increases size while pushing new items") {
    Allocator* alloc = alloc_bump_create_stack(1024);

    const u32 amountToPush = 33;

    DynArray array = dynarray_create_t(alloc, u64, 8);
    for (u32 i = 0; i != amountToPush; ++i) {
      dynarray_push(&array, 1);
      check_eq_int(array.size, i + 1);
    }
    dynarray_destroy(&array);
  }

  it("decreases size while popping items") {
    const u32 startingSize = 33;

    DynArray array = dynarray_create_over_t(mem_stack(512), u64);
    dynarray_resize(&array, startingSize);

    for (u32 i = startingSize; i-- != 0;) {
      dynarray_pop(&array, 1);
      check_eq_int(array.size, i);
    }
    dynarray_destroy(&array);
  }

  it("updates the size while removing elements") {
    struct {
      u32 removeIdx;
      u32 removeCount;
    } const data[] = {
        {0, 3},
        {1, 3},
        {5, 3},
        {7, 3},
        {9, 1},
        {0, 10},
    };
    const u64 values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    DynArray array = dynarray_create_over_t(mem_stack(256), u64);
    for (usize i = 0; i != array_elems(data); ++i) {
      dynarray_clear(&array);
      mem_cpy(dynarray_push(&array, array_elems(values)), array_mem(values));

      dynarray_remove(&array, data[i].removeIdx, data[i].removeCount);
      check_eq_int(array.size, array_elems(values) - data[i].removeCount);

      dynarray_for_t(&array, u64, val, {
        if (val_i < data[i].removeIdx)
          check_eq_int(*val, values[val_i]);
        else
          check_eq_int(*val, values[data[i].removeCount + val_i]);
      });
    }
    dynarray_destroy(&array);
  }

  it("moves the last element into the removed slot when using remove_unordered") {
    struct {
      const u16* initial;
      usize      initialSize;
      u32        removeIdx;
      u32        removeCount;
      u16*       expected;
    } const data[] = {
        {(u16[]){1, 2, 3, 4, 5}, 5, 0, 1, (u16[]){5, 2, 3, 4}},
        {(u16[]){1, 2, 3, 4, 5}, 5, 1, 1, (u16[]){1, 5, 3, 4}},
        {(u16[]){1, 2, 3, 4, 5}, 5, 0, 2, (u16[]){4, 5, 3}},
        {(u16[]){1, 2, 3, 4, 5}, 5, 0, 3, (u16[]){4, 5}},
        {(u16[]){1, 2, 3, 4, 5}, 5, 0, 4, (u16[]){5}},
        {(u16[]){1, 2, 3, 4, 5}, 5, 0, 5, null},
        {(u16[]){1, 2, 3, 4, 5, 6}, 6, 2, 1, (u16[]){1, 2, 6, 4, 5}},
        {(u16[]){1, 2, 3, 4, 5, 6}, 6, 2, 2, (u16[]){1, 2, 5, 6, 4}},
        {(u16[]){1, 2, 3, 4, 5, 6}, 6, 5, 1, (u16[]){1, 2, 3, 4, 5}},
        {(u16[]){1, 2, 3, 4, 5, 6}, 6, 4, 2, (u16[]){1, 2, 3, 4}},
    };

    DynArray array = dynarray_create_over_t(mem_stack(256), u16);
    for (usize i = 0; i != array_elems(data); ++i) {
      dynarray_clear(&array);

      mem_cpy(
          dynarray_push(&array, data[i].initialSize),
          mem_create(data[i].initial, sizeof(u16) * data[i].initialSize));

      dynarray_remove_unordered(&array, data[i].removeIdx, data[i].removeCount);
      check_eq_int(array.size, data[i].initialSize - data[i].removeCount);

      dynarray_for_t(&array, u16, val, { check_eq_int(*val, data[i].expected[val_i]); });
    }
    dynarray_destroy(&array);
  }

  it("updates the size when inserting elements") {
    struct {
      u32 insertIdx;
      u32 insertCount;
    } const data[] = {
        {0, 3},
        {1, 3},
        {5, 5},
        {10, 10},
    };
    const u32 values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    DynArray array = dynarray_create_over_t(mem_stack(256), u32);
    for (usize i = 0; i != array_elems(data); ++i) {
      dynarray_clear(&array);

      mem_cpy(dynarray_push(&array, array_elems(values)), array_mem(values));

      mem_set(dynarray_insert(&array, data[i].insertIdx, data[i].insertCount), 0xBB);
      check_eq_int(array.size, array_elems(values) + data[i].insertCount);

      dynarray_for_t(&array, u32, val, {
        if (val_i < data[i].insertIdx)
          check_eq_int(*val, values[val_i]);
        else if (val_i < data[i].insertIdx + data[i].insertCount)
          check_eq_int(*val, 0xBBBBBBBB);
        else
          check_eq_int(*val, values[val_i - data[i].insertCount]);
      });
    }
    dynarray_destroy(&array);
  }

  it("can insert elements sorted") {
    const u32 values[]   = {3, 6, 5, 9, 15, 10, 4, 13, 7, 1, 8, 12, 14, 11, 2};
    const u32 expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    DynArray array = dynarray_create_over_t(mem_stack(256), u32);

    array_for_t(values, u32, val, {
      // TODO: It might make sense to add an helper macro to 'core_dynarray.h' to avoid the casting?
      *mem_as_t(dynarray_insert_sorted(&array, 1, compare_u32, val), u32) = *val;
    });

    check_eq_int(array.size, array_elems(values));
    dynarray_for_t(&array, u32, val, { check_eq_int(*val, expected[val_i]); });

    dynarray_destroy(&array);
  }

  it("can be sorted") {
    const u32 values[]   = {3, 6, 5, 9, 15, 10, 4, 13, 7, 1, 8, 12, 14, 11, 2};
    const u32 expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    DynArray array = dynarray_create_over_t(mem_stack(256), u32);
    mem_cpy(dynarray_push(&array, array_elems(values)), array_mem(values));

    dynarray_sort(&array, compare_u32);

    dynarray_for_t(&array, u32, val, { check_eq_int(*val, expected[val_i]); });

    dynarray_destroy(&array);
  }

  it("can be searched using a linear scan") {
    const u32 values[] = {3, 6, 5, 15, 10, 4, 13, 7, 1, 8, 12, 14, 11, 2};

    DynArray array = dynarray_create_over_t(mem_stack(256), u32);
    mem_cpy(dynarray_push(&array, array_elems(values)), array_mem(values));

    u32        target = 0;
    const u32* found  = null;

    target = 4;
    found  = dynarray_search_linear(&array, compare_u32, &target);
    check_require(found != null);
    check_eq_int(*found, 4);

    target = 2;
    found  = dynarray_search_linear(&array, compare_u32, &target);
    check_require(found != null);
    check_eq_int(*found, 2);

    target = 9;
    found  = dynarray_search_linear(&array, compare_u32, &target);
    check(found == null);

    dynarray_destroy(&array);
  }

  it("can be searched using a binary scan") {
    const u32 values[] = {1, 2, 5, 7, 8, 9, 10, 12, 13, 15};

    DynArray array = dynarray_create_over_t(mem_stack(256), u32);
    mem_cpy(dynarray_push(&array, array_elems(values)), array_mem(values));

    u32        target = 0;
    const u32* found  = null;

    target = 5;
    found  = dynarray_search_binary(&array, compare_u32, &target);
    check_require(found != null);
    check_eq_int(*found, 5);

    target = 15;
    found  = dynarray_search_binary(&array, compare_u32, &target);
    check_require(found != null);
    check_eq_int(*found, 15);

    target = 6;
    found  = dynarray_search_binary(&array, compare_u32, &target);
    check(found == null);

    dynarray_destroy(&array);
  }
}
