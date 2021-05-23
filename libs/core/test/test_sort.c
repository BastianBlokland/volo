#include "core_diag.h"
#include "core_math.h"
#include "core_memory.h"
#include "core_sort.h"

static void test_quicksort_integer(i16* initial, const usize size, const i16* expected) {
  Mem mem = mem_create(initial, sizeof(i16) * size);
  quicksort(mem_begin(mem), mem_end(mem), sizeof(i16), &compare_i16);

  for (u32 i = 0; i != size; ++i) {
    diag_assert(initial[i] == expected[i]);
  }
}

static void test_quicksort_integer_reverse(i16* initial, const usize size, const i16* expected) {
  Mem mem = mem_create(initial, sizeof(i16) * size);
  quicksort(mem_begin(mem), mem_end(mem), sizeof(i16), &compare_i16_reverse);

  for (u32 i = 0; i != size; ++i) {
    diag_assert(initial[i] == expected[i]);
  }
}

void test_sort() {
  test_quicksort_integer((i16[]){1}, 1, (i16[]){1});
  test_quicksort_integer((i16[]){2, 1}, 2, (i16[]){1, 2});
  test_quicksort_integer((i16[]){1, 2, 3, 4, 5}, 5, (i16[]){1, 2, 3, 4, 5});
  test_quicksort_integer((i16[]){5, 4, 3, 2, 1}, 5, (i16[]){1, 2, 3, 4, 5});
  test_quicksort_integer((i16[]){5, 2, 4, 1, 3}, 5, (i16[]){1, 2, 3, 4, 5});
  test_quicksort_integer((i16[]){1, 1, 1, 1, 1}, 5, (i16[]){1, 1, 1, 1, 1});
  test_quicksort_integer((i16[]){1, 1, 1, 2, 1}, 5, (i16[]){1, 1, 1, 1, 2});

  test_quicksort_integer_reverse((i16[]){5, 4, 3, 2, 1}, 5, (i16[]){5, 4, 3, 2, 1});
  test_quicksort_integer_reverse((i16[]){1, 2, 3, 4, 5}, 5, (i16[]){5, 4, 3, 2, 1});
  test_quicksort_integer_reverse((i16[]){1, 1, 1, 2, 1}, 5, (i16[]){2, 1, 1, 1, 1});
}
