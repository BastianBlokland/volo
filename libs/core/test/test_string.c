#include "core_diag.h"
#include "core_sentinel.h"
#include "core_string.h"

static void test_string_len() {
  diag_assert(string_empty.size == 0);
  diag_assert(string_lit("").size == 0);
  diag_assert(string_lit("H").size == 1);
  diag_assert(string_lit("Hello World").size == 11);
}

static void test_string_is_empty() {
  diag_assert(string_is_empty(string_empty));
  diag_assert(string_is_empty(string_lit("")));
  diag_assert(!string_is_empty(string_lit("Hello World")));
}

static void test_string_last() {
  diag_assert(*string_last(string_lit("Hello World")) == 'd');
  diag_assert(*string_last(string_lit(" ")) == ' ');
}

static void test_string_cmp() {
  diag_assert(string_cmp(string_lit("a"), string_lit("a")) == 0);
  diag_assert(string_cmp(string_lit("a"), string_lit("b")) == -1);
  diag_assert(string_cmp(string_lit("b"), string_lit("a")) == 1);
  diag_assert(string_cmp(string_lit("April"), string_lit("March")) == -1);
  diag_assert(string_cmp(string_lit("March"), string_lit("December")) == 1);
}

static void test_string_eq() {
  diag_assert(string_eq(string_lit(""), string_lit("")));
  diag_assert(string_eq(string_lit("Hello World"), string_lit("Hello World")));

  diag_assert(!string_eq(string_lit(""), string_lit("H")));
  diag_assert(!string_eq(string_lit("Hello Worl"), string_lit("Hello World")));
  diag_assert(!string_eq(string_lit("ello World"), string_lit("Hello World")));
}

static void test_string_starts_with() {
  diag_assert(string_starts_with(string_lit(""), string_lit("")));
  diag_assert(string_starts_with(string_lit("Hello World"), string_lit("Hello")));
  diag_assert(string_starts_with(string_lit("Hello"), string_lit("Hello")));
  diag_assert(!string_starts_with(string_lit("Hell"), string_lit("Hello")));
  diag_assert(!string_starts_with(string_lit("Hello World"), string_lit("Stuff")));
}

static void test_string_ends_with() {
  diag_assert(string_ends_with(string_lit(""), string_lit("")));
  diag_assert(string_ends_with(string_lit("Hello World"), string_lit("World")));
  diag_assert(string_ends_with(string_lit("Hello"), string_lit("Hello")));
  diag_assert(!string_ends_with(string_lit("Hell"), string_lit("ello")));
  diag_assert(!string_ends_with(string_lit("Hello World"), string_lit("Stuff")));
}

static void test_string_slice() {
  diag_assert(string_eq(string_slice(string_lit("Hello World"), 0, 5), string_lit("Hello")));
  diag_assert(string_eq(string_slice(string_lit("Hello World"), 6, 5), string_lit("World")));
}

static void test_string_consume() {
  diag_assert(string_eq(string_consume(string_lit("Hello World"), 5), string_lit(" World")));
  diag_assert(string_eq(string_consume(string_lit(" "), 1), string_lit("")));
  diag_assert(string_eq(string_consume(string_lit(""), 0), string_lit("")));
  diag_assert(string_eq(string_consume(string_lit("Hello"), 0), string_lit("Hello")));
}

static void test_string_find_first_any() {
  diag_assert(string_find_first_any(string_lit(""), string_lit(" ")) == sentinel_usize);
  diag_assert(string_find_first_any(string_lit(""), string_lit("\0")) == sentinel_usize);
  diag_assert(string_find_first_any(string_lit("\0"), string_lit("\n\r\0")) == 0);
  diag_assert(string_find_first_any(string_lit("Hello World"), string_lit(" ")) == 5);
  diag_assert(string_find_first_any(string_lit("Hello World"), string_lit("or")) == 4);
  diag_assert(
      string_find_first_any(string_lit("Hello World"), string_lit("zqx")) == sentinel_usize);
}

static void test_string_find_last_any() {
  diag_assert(string_find_last_any(string_lit(""), string_lit(" ")) == sentinel_usize);
  diag_assert(string_find_last_any(string_lit(""), string_lit("\0")) == sentinel_usize);
  diag_assert(string_find_last_any(string_lit("\0"), string_lit("\n\r\0")) == 0);
  diag_assert(string_find_last_any(string_lit("Hello World"), string_lit(" ")) == 5);
  diag_assert(string_find_last_any(string_lit("Hello World"), string_lit("or")) == 8);
  diag_assert(string_find_last_any(string_lit("Hello World"), string_lit("d")) == 10);
  diag_assert(string_find_last_any(string_lit("Hello World"), string_lit("hH")) == 0);
  diag_assert(string_find_last_any(string_lit("Hello World"), string_lit("zqx")) == sentinel_usize);
}

void test_string() {
  test_string_len();
  test_string_is_empty();
  test_string_last();
  test_string_eq();
  test_string_starts_with();
  test_string_ends_with();
  test_string_cmp();
  test_string_slice();
  test_string_consume();
  test_string_find_first_any();
  test_string_find_last_any();
}
