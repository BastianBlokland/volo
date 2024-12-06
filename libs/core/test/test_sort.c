#include "check_spec.h"
#include "core_array.h"
#include "core_sort.h"

static i8 test_sort_i32_index_compare(const void* ctx, const usize a, const usize b) {
  const i32* data = ctx;
  return compare_i32(data + a, data + b);
}

static void test_sort_i32_index_swap(void* ctx, const usize a, const usize b) {
  i32* data = ctx;
  mem_swap(mem_var(data[a]), mem_var(data[b]));
}

spec(sort) {

  enum { MaxElemCount = 20 };

  struct {
    usize size;
    i32   values[MaxElemCount];
    i32   expected[MaxElemCount];
  } const i32Data[] = {
      {1, {1}, {1}},
      {2, {2, 1}, {1, 2}},
      {5, {1, 2, 3, 4, 5}, {1, 2, 3, 4, 5}},
      {5, {5, 4, 3, 2, 1}, {1, 2, 3, 4, 5}},
      {5, {5, 2, 4, 1, 3}, {1, 2, 3, 4, 5}},
      {5, {1, 1, 1, 1, 1}, {1, 1, 1, 1, 1}},
      {5, {1, 1, 1, 2, 1}, {1, 1, 1, 1, 2}},
      {9, {2, 3, 0, 1, -3, 4, -2, -1, -4}, {-4, -3, -2, -1, 0, 1, 2, 3, 4}},
      {20,
       {3, 16, 6, 5, 9, 15, 10, 4, 17, 13, 7, 1, 8, 20, 12, 14, 11, 19, 2, 18},
       {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}},
  };

  it("can sort i32 integers") {

    for (usize i = 0; i != array_elems(i32Data); ++i) {
      sort_quicksort_t(i32Data[i].values, i32Data[i].values + i32Data[i].size, i32, compare_i32);

      for (u32 j = 0; j != i32Data[i].size; ++j) {
        check_eq_int(i32Data[i].values[j], i32Data[i].expected[j]);
      }
    }
  }

  struct {
    usize  size;
    String values[MaxElemCount];
    String expected[MaxElemCount];
  } const stringData[] = {
      {5,
       {string_lit("B"), string_lit("E"), string_lit("A"), string_lit("C"), string_lit("D")},
       {string_lit("A"), string_lit("B"), string_lit("C"), string_lit("D"), string_lit("E")}},
      {12,
       {string_lit("January"),
        string_lit("February"),
        string_lit("March"),
        string_lit("April"),
        string_lit("May"),
        string_lit("June"),
        string_lit("July"),
        string_lit("August"),
        string_lit("September"),
        string_lit("October"),
        string_lit("November"),
        string_lit("December")},
       {string_lit("April"),
        string_lit("August"),
        string_lit("December"),
        string_lit("February"),
        string_lit("January"),
        string_lit("July"),
        string_lit("June"),
        string_lit("March"),
        string_lit("May"),
        string_lit("November"),
        string_lit("October"),
        string_lit("September")}},
  };

  it("can sort strings") {
    for (usize i = 0; i != array_elems(stringData); ++i) {
      sort_quicksort_t(
          stringData[i].values, stringData[i].values + stringData[i].size, String, compare_string);

      for (u32 j = 0; j != stringData[i].size; ++j) {
        check_eq_string(stringData[i].values[j], stringData[i].expected[j]);
      }
    }
  }

  it("can sort i32 integers using indices") {
    for (usize i = 0; i != array_elems(i32Data); ++i) {
      sort_index_quicksort(
          (void*)i32Data[i].values,
          0,
          i32Data[i].size,
          test_sort_i32_index_compare,
          test_sort_i32_index_swap);

      for (u32 j = 0; j != i32Data[i].size; ++j) {
        check_eq_int(i32Data[i].values[j], i32Data[i].expected[j]);
      }
    }
  }
}
