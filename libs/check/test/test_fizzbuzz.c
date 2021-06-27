#include "check_spec.h"

static String fizzbuzz(const i32 i) {
  const bool fizz = (i % 3) == 0;
  const bool buzz = (i % 5) == 0;
  if (fizz && buzz) {
    return string_lit("FizzBuzz");
  }
  if (fizz) {
    return string_lit("Fizz");
  }
  if (buzz) {
    return string_lit("Buzz");
  }
  return fmt_write_scratch("{}", fmt_int(i));
}

spec(fizzbuzz) {
  it("returns Fizz for multiples of 3") {
    check_eq_string(fizzbuzz(3), string_lit("Fizz"));
    check_eq_string(fizzbuzz(6), string_lit("Fizz"));
    check_eq_string(fizzbuzz(9), string_lit("Fizz"));
  }
  it("returns Buzz for multiples of 5") {
    check_eq_string(fizzbuzz(5), string_lit("Buzz"));
    check_eq_string(fizzbuzz(10), string_lit("Buzz"));
    check_eq_string(fizzbuzz(20), string_lit("Buzz"));
  }
  it("returns FizzBuzz for multiples of 3 and 5") {
    check_eq_string(fizzbuzz(15), string_lit("FizzBuzz"));
    check_eq_string(fizzbuzz(30), string_lit("FizzBuzz"));
  }
  it("otherwise returns the given integer") {
    check_eq_string(fizzbuzz(1), string_lit("1"));
    check_eq_string(fizzbuzz(2), string_lit("2"));
    check_eq_string(fizzbuzz(4), string_lit("4"));
  }
}
