#include "check_spec.h"

#include "utils_internal.h"

spec(color) {

  it("sums all components when adding") {
    check_eq_color(
        geo_color_add(geo_color(1, -2.1f, 3, 4), geo_color(2, 3.2f, 4, 5)),
        geo_color(3, 1.1f, 7, 9));
    check_eq_color(
        geo_color_add(geo_color(1, 2, 3, 0), geo_color(4, 5, 6, 0)), geo_color(5, 7, 9, 0));
  }

  it("multiplies each component by the scalar when multiplying") {
    check_eq_color(geo_color_mul(geo_color(5, -2.1f, 6, 8), 2), geo_color(10, -4.2f, 12, 16));
    check_eq_color(geo_color_mul(geo_color(1, 2, 3, 0), -2), geo_color(-2, -4, -6, 0));
  }

  it("can linearly interpolate colors") {
    const GeoColor c1 = geo_color(10, 20, 10, 1);
    const GeoColor c2 = geo_color(20, 40, 20, 1);
    const GeoColor c3 = geo_color(15, 30, 15, 1);
    check_eq_color(geo_color_lerp(c1, c2, .5f), c3);
  }

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
