#include "check/spec.h"
#include "cli/validate.h"

spec(validate) {

  it("supports validating signed integers") {
    check(cli_validate_i64(string_lit("42")));
    check(cli_validate_i64(string_lit("-42")));

    check(!cli_validate_i64(string_lit("Hello")));
    check(!cli_validate_i64(string_lit("--42")));
  }

  it("supports validating unsigned integers") {
    check(cli_validate_u16(string_lit("42")));
    check(cli_validate_u64(string_lit("42")));
    check(!cli_validate_u16(string_lit("66000")));
    check(cli_validate_u64(string_lit("60000")));

    check(!cli_validate_u64(string_lit("Hello")));
    check(!cli_validate_u64(string_lit("-42")));
  }

  it("supports validating f64's") {
    check(cli_validate_f64(string_lit("42.1337e-2")));

    check(!cli_validate_f64(string_lit("Hello")));
    check(!cli_validate_f64(string_lit("42.1337f-2")));
  }
}
