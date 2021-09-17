#include "check_spec.h"
#include "cli_validate.h"

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

  it("supports validating f64's") {
    check(cli_validate_f64(string_lit("42.1337e-2")));

    check(!cli_validate_f64(string_lit("Hello")));
    check(!cli_validate_f64(string_lit("42.1337f-2")));
  }
}
