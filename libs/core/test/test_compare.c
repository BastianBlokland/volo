#include "core_compare.h"
#include "core_diag.h"
#include "core_string.h"

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

  diag_assert(compare_u32(&(u32){42}, &(u32){1337}) == -1);
  diag_assert(compare_u32(&(u32){1337}, &(u32){42}) == 1);

  diag_assert(compare_u32_reverse(&(u32){42}, &(u32){1337}) == 1);
  diag_assert(compare_u32_reverse(&(u32){1337}, &(u32){42}) == -1);

  diag_assert(compare_float(&(float){1.1f}, &(float){1.3f}) == -1);
  diag_assert(compare_float(&(float){1.3f}, &(float){1.1f}) == 1);
  diag_assert(compare_float(&(float){1.3f}, &(float){1.3f}) == 0);

  diag_assert(compare_float_reverse(&(float){1.1f}, &(float){1.3f}) == 1);
  diag_assert(compare_float_reverse(&(float){1.3f}, &(float){1.1f}) == -1);
  diag_assert(compare_float_reverse(&(float){1.3f}, &(float){1.3f}) == 0);

  diag_assert(compare_string(&string_lit("a"), &string_lit("b")) == -1);
  diag_assert(compare_string(&string_lit("a"), &string_lit("a")) == 0);
  diag_assert(compare_string(&string_lit("b"), &string_lit("a")) == 1);

  diag_assert(compare_string_reverse(&string_lit("a"), &string_lit("b")) == 1);
  diag_assert(compare_string_reverse(&string_lit("a"), &string_lit("a")) == 0);
  diag_assert(compare_string_reverse(&string_lit("b"), &string_lit("a")) == -1);
}

void test_compare() { test_compare_primitive_types(); }
