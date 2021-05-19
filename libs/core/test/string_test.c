#include "core_diag.h"
#include "core_string.h"

static void test_string_len() {
  diag_assert(string_empty().size == 0);
  diag_assert(string_from_lit("").size == 0);
  diag_assert(string_from_lit("H").size == 1);
  diag_assert(string_from_lit("Hello World").size == 11);
}

static void test_string_cmp() {
  diag_assert(string_cmp(string_from_lit(""), string_from_lit("")) == 0);
  diag_assert(string_cmp(string_from_lit("a"), string_from_lit("")) > 0);
  diag_assert(string_cmp(string_from_lit(""), string_from_lit("a")) < 0);
  diag_assert(string_cmp(string_from_lit("a"), string_from_lit("a")) == 0);
  diag_assert(string_cmp(string_from_lit("a"), string_from_lit("b")) < 0);
  diag_assert(string_cmp(string_from_lit("b"), string_from_lit("a")) > 0);
}

static void test_string_eq() {
  diag_assert(string_eq(string_from_lit(""), string_from_lit("")));
  diag_assert(string_eq(string_from_lit("Hello World"), string_from_lit("Hello World")));

  diag_assert(!string_eq(string_from_lit(""), string_from_lit("H")));
  diag_assert(!string_eq(string_from_lit("Hello Worl"), string_from_lit("Hello World")));
  diag_assert(!string_eq(string_from_lit("ello World"), string_from_lit("Hello World")));
}

void test_string() {
  test_string_len();
  test_string_eq();
  test_string_cmp();
}
