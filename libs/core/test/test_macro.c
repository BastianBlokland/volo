#include "core_diag.h"
#include "core_macro.h"

static void test_macro_count_va_args() {
  _Static_assert(COUNT_VA_ARGS() == 0, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1) == 1, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1, 2) == 2, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1, 2, 3) == 3, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1, 2, 3, 4) == 4, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1, 2, 3, 4, 5) == 5, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6) == 6, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7) == 7, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8) == 8, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9) == 9, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 10, "test_macro failed");
  _Static_assert(COUNT_VA_ARGS(((1, 3, 4)), ("hello", {2})) == 2, "test_macro failed");
}

void test_macro() { test_macro_count_va_args(); }
