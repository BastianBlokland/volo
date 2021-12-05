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

static TestElem* test_search_linear(const TestElem* begin, const TestElem* end, const i32 key) {
  return search_linear_t(
      begin, end, TestElem, compare_testelem, mem_struct(TestElem, .key = key).ptr);
}

static TestElem* test_search_binary(const TestElem* begin, const TestElem* end, const i32 key) {
  return search_binary_t(
      begin, end, TestElem, compare_testelem, mem_struct(TestElem, .key = key).ptr);
}

static TestElem*
test_search_binary_greater(const TestElem* begin, const TestElem* end, const i32 key) {
  return search_binary_greater_t(
      begin, end, TestElem, compare_testelem, mem_struct(TestElem, .key = key).ptr);
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

    array_for_t(data, TestElem, elem) {
      found = test_search_linear(data, data + array_elems(data), elem->key);
      check_require(found != null);
      check_eq_string(found->value, elem->value);
    }

    found = test_search_linear(data, data + array_elems(data), 42);
    check(found == null);

    // NOTE: Test an empty collection.
    found = test_search_linear(data, data, 1);
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

    array_for_t(data, TestElem, elem) {
      found = test_search_binary(data, data + array_elems(data), elem->key);
      check_require(found != null);
      check_eq_string(found->value, elem->value);
    }

    found = test_search_binary(data, data + array_elems(data), 10);
    check(found == null);

    found = test_search_binary(data, data + array_elems(data), -1000);
    check(found == null);

    found = test_search_binary(data, data + array_elems(data), 1000);
    check(found == null);

    // NOTE: Test an empty collection.
    found = test_search_binary(data, data, 1);
    check(found == null);
  }

  it("can find greater elements in ordered data") {
    const TestElem data[] = {
        {.key = -42, .value = string_lit("A")},
        {.key = 2, .value = string_lit("B")},
        {.key = 8, .value = string_lit("C")},
        {.key = 9, .value = string_lit("D1")},
        {.key = 9, .value = string_lit("D2")},
        {.key = 12, .value = string_lit("E")},
        {.key = 60, .value = string_lit("F")},
        {.key = 256, .value = string_lit("G")},
    };

    const TestElem* found = null;

    found = test_search_binary_greater(data, data + array_elems(data), 10);
    check_require(found != null);
    check_eq_string(found->value, string_lit("E"));

    found = test_search_binary_greater(data, data + array_elems(data), 8);
    check_require(found != null);
    check_eq_string(found->value, string_lit("D1"));

    found = test_search_binary_greater(data, data + array_elems(data), 9);
    check_require(found != null);
    check_eq_string(found->value, string_lit("E"));

    found = test_search_binary_greater(data, data + array_elems(data), -100);
    check_require(found != null);
    check_eq_string(found->value, string_lit("A"));

    found = test_search_binary_greater(data, data + array_elems(data), 61);
    check_require(found != null);
    check_eq_string(found->value, string_lit("G"));

    found = test_search_binary_greater(data, data + array_elems(data), 256);
    check(found == null);

    found = test_search_binary_greater(data, data + array_elems(data), 257);
    check(found == null);

    found = test_search_binary_greater(data, data + array_elems(data), 1000);
    check(found == null);
  }
}
