#include "core_compare.h"
#include "core_diag.h"

static void test_compare_primitive_types() {
  diag_assert(compare_i32(&(i32){1}, &(i32){2}) == -1);
  diag_assert(compare_i32(&(i32){1}, &(i32){1}) == 0);
  diag_assert(compare_i32(&(i32){2}, &(i32){1}) == 1);
  diag_assert(compare_i32(&(i32){-2}, &(i32){-1}) == -1);
  diag_assert(compare_i32(&(i32){-2}, &(i32){-3}) == 1);
  diag_assert(compare_i32(&(i32){-2}, &(i32){-2}) == 0);

  diag_assert(compare_i32_reverse(&(i32){1}, &(i32){2}) == 1);
  diag_assert(compare_i32_reverse(&(i32){-2}, &(i32){-1}) == 1);
  diag_assert(compare_i32_reverse(&(i32){-2}, &(i32){-3}) == -1);
  diag_assert(compare_i32_reverse(&(i32){-2}, &(i32){-2}) == 0);

  diag_assert(compare_float(&(float){1.1f}, &(float){1.3f}) == -1);
  diag_assert(compare_float(&(float){1.3f}, &(float){1.1f}) == 1);
  diag_assert(compare_float(&(float){1.3f}, &(float){1.3f}) == 0);

  diag_assert(compare_float_reverse(&(float){1.1f}, &(float){1.3f}) == 1);
  diag_assert(compare_float_reverse(&(float){1.3f}, &(float){1.1f}) == -1);
  diag_assert(compare_float_reverse(&(float){1.3f}, &(float){1.3f}) == 0);
}

void test_compare() { test_compare_primitive_types(); }
