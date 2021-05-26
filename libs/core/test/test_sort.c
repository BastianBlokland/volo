#include "core_diag.h"
#include "core_math.h"
#include "core_sort.h"

// clang-format off
// Test data: (Size, Unsorted, Sorted)
#define TEST_SORT_INTEGER_DATA                                                                     \
  X(1,  Y(1),              Y(1))                                                                   \
  X(2,  Y(2, 1),           Y(1, 2))                                                                \
  X(5,  Y(1, 2, 3, 4, 5),  Y(1, 2, 3, 4, 5))                                                       \
  X(5,  Y(5, 4, 3, 2, 1),  Y(1, 2, 3, 4, 5))                                                       \
  X(5,  Y(5, 2, 4, 1, 3),  Y(1, 2, 3, 4, 5))                                                       \
  X(5,  Y(1, 1, 1, 1, 1),  Y(1, 1, 1, 1, 1))                                                       \
  X(5,  Y(1, 1, 1, 2, 1),  Y(1, 1, 1, 1, 2))                                                       \
  X(9,  Y(2, 3, 0, 1, -3, 4, -2, -1, -4),                                                          \
        Y(-4, -3, -2, -1, 0, 1, 2, 3, 4))                                                          \
  X(20, Y(3, 16, 6, 5, 9, 15, 10, 4, 17, 13, 7, 1, 8, 20, 12, 14, 11, 19, 2, 18),                  \
        Y(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20))
// clang-format on

static void test_sort_i32(usize size, i32* values, i32* expected, SortFunc func, CompareFunc comp) {
  func((u8*)values, (u8*)(values + size), sizeof(i32), comp);
  for (u32 i = 0; i != size; ++i) {
    diag_assert(values[i] == expected[i]);
  }
}

// clang-format off
// Test data: (Size, Unsorted, Sorted)
#define TEST_SORT_STRING_DATA                                                                      \
  X(5,  Y(Z("B"), Z("E"), Z("A"), Z("C"), Z("D")),                                                 \
        Y(Z("A"), Z("B"), Z("C"), Z("D"), Z("E")))                                                 \
  X(12, Y(Z("January"), Z("February"), Z("March"), Z("April"), Z("May"), Z("June"), Z("July"),     \
          Z("August"), Z("September"), Z("October"), Z("November"), Z("December")),                \
        Y(Z("April"), Z("August"), Z("December"), Z("February"), Z("January"), Z("July"),          \
          Z("June"), Z("March"), Z("May"), Z("November"), Z("October"), Z("September")))
// clang-format on

static void
test_sort_string(usize size, String* values, String* expected, SortFunc func, CompareFunc comp) {
  func((u8*)values, (u8*)(values + size), sizeof(String), comp);
  for (u32 i = 0; i != size; ++i) {
    diag_assert(string_eq(values[i], expected[i]));
  }
}

void test_sort() {
  // Test integer values.
#define Y(...)                                                                                     \
  (i32[]) { __VA_ARGS__ }
#define X(_SIZE_, _VALUES_, _SORTED_)                                                              \
  test_sort_i32(_SIZE_, _VALUES_, _SORTED_, sort_bubblesort, compare_i32);                         \
  test_sort_i32(_SIZE_, _VALUES_, _SORTED_, sort_quicksort, compare_i32);
  TEST_SORT_INTEGER_DATA

#undef X
#undef Y

  // Test string values.
#define Z(_STR_) string_lit(_STR_)
#define Y(...)                                                                                     \
  (String[]) { __VA_ARGS__ }
#define X(_SIZE_, _VALUES_, _SORTED_)                                                              \
  test_sort_string(_SIZE_, _VALUES_, _SORTED_, sort_bubblesort, compare_string);                   \
  test_sort_string(_SIZE_, _VALUES_, _SORTED_, sort_quicksort, compare_string);
  TEST_SORT_STRING_DATA

#undef X
#undef Y
#undef Z
}
