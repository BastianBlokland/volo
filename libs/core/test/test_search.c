#include "check_spec.h"
#include "core_array.h"
#include "core_search.h"

typedef struct {
  i32    key;
  String value;
} TestElem;

static i8 compare_testelem(const void* a, const void* b) {
  return compare_i32(field_ptr(a, TestElem, key), field_ptr(b, TestElem, key));
}

spec(search) {

  it("can find elements in unordered data") {
    const TestElem data[] = {
        {.key = 9, .value = string_lit("A")},
        {.key = 8, .value = string_lit("B")},
        {.key = 2, .value = string_lit("C")},
        {.key = 60, .value = string_lit("D")},
        {.key = 12, .value = string_lit("E")},
        {.key = -42, .value = string_lit("F")},
        {.key = 256, .value = string_lit("G")},
    };

    const TestElem* found = null;

    array_for_t(data, TestElem, elem, {
      found = search_linear_struct_t(
          data, data + array_elems(data), TestElem, compare_testelem, .key = elem->key);
      check_require(found != null);
      check_eq_string(found->value, elem->value);
    });

    found = search_linear_struct_t(
        data, data + array_elems(data), TestElem, compare_testelem, .key = 42);
    check(found == null);

    // NOTE: Test an empty collection.
    found = search_linear_struct_t(data, data, TestElem, compare_testelem, .key = 1);
    check(found == null);
  }

  it("can find elements in ordered data") {
    const TestElem data[] = {
        {.key = -42, .value = string_lit("A")},
        {.key = 2, .value = string_lit("B")},
        {.key = 8, .value = string_lit("C")},
        {.key = 9, .value = string_lit("D")},
        {.key = 12, .value = string_lit("E")},
        {.key = 60, .value = string_lit("F")},
        {.key = 256, .value = string_lit("G")},
    };

    const TestElem* found = null;

    array_for_t(data, TestElem, elem, {
      found = search_binary_struct_t(
          data, data + array_elems(data), TestElem, compare_testelem, .key = elem->key);
      check_require(found != null);
      check_eq_string(found->value, elem->value);
    });

    found = search_binary_struct_t(
        data, data + array_elems(data), TestElem, compare_testelem, .key = 10);
    check(found == null);

    found = search_binary_struct_t(
        data, data + array_elems(data), TestElem, compare_testelem, .key = -1000);
    check(found == null);

    found = search_binary_struct_t(
        data, data + array_elems(data), TestElem, compare_testelem, .key = 1000);
    check(found == null);

    // NOTE: Test an empty collection.
    found = search_binary_struct_t(data, data, TestElem, compare_testelem, .key = 1);
    check(found == null);
  }
}
