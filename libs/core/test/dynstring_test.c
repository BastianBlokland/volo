#include "core_diag.h"
#include "core_dynstring.h"

static void test_dynstring_new_string_is_empty() {
  DynString string = dynstring_create(&g_allocatorHeap, 8);
  diag_assert(string.size == 0);
  dynstring_destroy(&string);
}

static void test_dynstring_append() {
  DynString string = dynstring_create(&g_allocatorHeap, 8);

  dynstring_append(&string, string_from_lit("Hello"));
  diag_assert(string_eq(dynstring_view(&string), string_from_lit("Hello")));

  dynstring_append(&string, string_from_lit(" "));
  diag_assert(string_eq(dynstring_view(&string), string_from_lit("Hello ")));

  dynstring_append(&string, string_from_lit("World"));
  diag_assert(string_eq(dynstring_view(&string), string_from_lit("Hello World")));

  dynstring_destroy(&string);
}

void test_dynstring() {
  test_dynstring_new_string_is_empty();
  test_dynstring_append();
}
