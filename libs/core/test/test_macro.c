#include "check_spec.h"
#include "core_annotation.h"
#include "core_macro.h"

spec(macro) {

  it("can count the number of VA_ARGs") {
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
    ASSERT(COUNT_VA_ARGS(((1, 3, 4)), ("hello", {2})) == 2, "test_macro failed");
  }

  it("can skip the first VA_ARG argument") {
    ASSERT(VA_ARGS_SKIP_FIRST(1, 2) == 2, "test_macro failed");
  }
}
