#include "check_spec.h"
#include "core_array.h"

spec(array) {

  it("can iterate over a static array") {
    String array[8];
    for (usize i = 0; i != array_elems(array); ++i) {
      array[i] = string_lit("Hello World");
    }

    usize foundCount = 0;
    array_for_t(array, String, str) {
      check_eq_string(*str, string_lit("Hello World"));
      ++foundCount;
    }
    check_eq_int(foundCount, array_elems(array));
  }

  it("can iterate over a heap-array") {
    String storage[8];
    for (usize i = 0; i != array_elems(storage); ++i) {
      storage[i] = string_lit("Hello World");
    }

    HeapArray_t(String) array = {.values = storage, .count = array_elems(storage)};

    usize foundCount = 0;
    heap_array_for_t(array, String, str) {
      check_eq_string(*str, string_lit("Hello World"));
      ++foundCount;
    }
    check_eq_int(foundCount, array.count);
  }
}
