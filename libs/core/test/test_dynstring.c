#include "core_diag.h"
#include "core_dynstring.h"

static void test_dynstring_new_string_is_empty() {
  DynString string = dynstring_create_over(mem_stack(128));
  diag_assert(string.size == 0);
  dynstring_destroy(&string);
}

static void test_dynstring_append() {
  DynString string = dynstring_create_over(mem_stack(128));

  dynstring_append(&string, string_lit("Hello"));
  diag_assert(string_eq(dynstring_view(&string), string_lit("Hello")));

  dynstring_append(&string, string_lit(" "));
  diag_assert(string_eq(dynstring_view(&string), string_lit("Hello ")));

  dynstring_append(&string, string_lit("World"));
  diag_assert(string_eq(dynstring_view(&string), string_lit("Hello World")));

  dynstring_append(&string, string_empty());
  diag_assert(string_eq(dynstring_view(&string), string_lit("Hello World")));

  dynstring_destroy(&string);
}

static void test_dynstring_append_char() {
  DynString string = dynstring_create_over(mem_stack(128));

  dynstring_append_char(&string, 'H');
  dynstring_append_char(&string, 'e');
  dynstring_append_char(&string, 'l');
  dynstring_append_char(&string, 'l');
  dynstring_append_char(&string, 'o');

  diag_assert(string_eq(dynstring_view(&string), string_lit("Hello")));

  dynstring_destroy(&string);
}

void test_dynstring() {
  test_dynstring_new_string_is_empty();
  test_dynstring_append();
  test_dynstring_append_char();
}
