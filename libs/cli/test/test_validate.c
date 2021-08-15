#include "cli_validate.h"

#include "check_spec.h"

spec(validate) {

  it("supports validating i64's") {
    check(cli_validate_i64(string_lit("42")));
    check(cli_validate_i64(string_lit("-42")));

    check(!cli_validate_i64(string_lit("Hello")));
    check(!cli_validate_i64(string_lit("--42")));
  }

  it("supports validating u64's") {
    check(cli_validate_u64(string_lit("42")));

    check(!cli_validate_u64(string_lit("Hello")));
    check(!cli_validate_u64(string_lit("-42")));
  }

  it("supports validating booleans") {
    check(cli_validate_bool(string_lit("true")));
    check(cli_validate_bool(string_lit("false")));

    check(!cli_validate_bool(string_lit("Hello")));
    check(!cli_validate_bool(string_lit("42")));
  }
}
