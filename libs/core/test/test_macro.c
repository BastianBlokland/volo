#include "check_spec.h"
#include "core_annotation.h"
#include "core_macro.h"

spec(macro) {

  it("can count the number of VA_ARGs") {
    // clang-format off
    ASSERT(COUNT_VA_ARGS() == 0, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1) == 1, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2) == 2, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3) == 3, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4) == 4, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5) == 5, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6) == 6, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7) == 7, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8) == 8, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9) == 9, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 10, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11) == 11, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) == 12, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13) == 13, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14) == 14, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15) == 15, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16) == 16, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17) == 17, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18) == 18, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19) == 19, "test_macro failed");
    ASSERT(COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20) == 20, "test_macro failed");
    // clang-format off
    ASSERT(COUNT_VA_ARGS(((1, 3, 4)), ("hello", {2})) == 2, "test_macro failed");
  }

  it("can skip the first VA_ARG argument") {
    ASSERT(VA_ARGS_SKIP_FIRST(1, 2) == 2, "test_macro failed");
  }
}
