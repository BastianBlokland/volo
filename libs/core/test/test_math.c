#include "core_diag.h"
#include "core_math.h"

static void test_math_min() {
  diag_assert(math_min(1, 0) == 0);
  diag_assert(math_min(0, 0) == 0);
  diag_assert(math_min(1, -1) == -1);
  diag_assert(math_min(-1, 0) == -1);

  diag_assert(math_min(-1.0f, 0.0f) == -1.0f);
  diag_assert(math_min(-1.1f, -1.2f) == -1.2f);
}

static void test_math_max() {
  diag_assert(math_max(1, 0) == 1);
  diag_assert(math_max(0, 0) == 0);
  diag_assert(math_max(-1, 1) == 1);
  diag_assert(math_max(-1, -2) == -1);

  diag_assert(math_max(-1.0f, 0.1f) == 0.1f);
  diag_assert(math_max(-1.1f, -1.2f) == -1.1f);
}

static void test_math_sign() {
  diag_assert(math_sign(-42) == -1);
  diag_assert(math_sign(42) == 1);
  diag_assert(math_sign(0) == 0);

  diag_assert(math_sign(-0.1f) == -1);
  diag_assert(math_sign(0.1f) == 1);
  diag_assert(math_sign(0.0f) == 0);
}

static void test_math_abs() {
  diag_assert(math_abs(-42) == 42);
  diag_assert(math_abs(42) == 42);
  diag_assert(math_abs(0) == 0);
  diag_assert(math_abs(-1.25) == 1.25);
  diag_assert(math_abs(0.0) == 0.0);
}

void test_math() {
  test_math_min();
  test_math_max();
  test_math_sign();
  test_math_abs();
}
