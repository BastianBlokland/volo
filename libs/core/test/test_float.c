#include "core_diag.h"
#include "core_float.h"

static void test_float_isnan() {
  diag_assert(float_isnan(f32_nan));
  diag_assert(float_isnan(f64_nan));
}

static void test_float_isinf() {
  diag_assert(float_isinf(f32_inf));
  diag_assert(float_isinf(f64_inf));
}

void test_float() {
  test_float_isnan();
  test_float_isinf();
}
