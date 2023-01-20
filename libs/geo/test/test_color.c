#include "check_spec.h"

#include "utils_internal.h"

spec(color) {

  it("lists all components when formatted") {
    check_eq_string(
        fmt_write_scratch("{}", geo_color_fmt(geo_color_white)), string_lit("1, 1, 1, 1"));
    check_eq_string(
        fmt_write_scratch("{}", geo_color_fmt(geo_color_red)), string_lit("1, 0, 0, 1"));
    check_eq_string(
        fmt_write_scratch("{}", geo_color_fmt(geo_color(42, 1337, 1, 0.42f))),
        string_lit("42, 1337, 1, 0.42"));
  }
}
