#include "check_spec.h"
#include "geo_vector.h"

spec(vector) {

  it("compares the magnitude of the difference vector to a threshold when equated") {
    check(geo_vector_equal(geo_forward, geo_forward, 0));
    check(geo_vector_equal(geo_forward, geo_forward, 1e-6));
    check(!geo_vector_equal(geo_forward, geo_backward, 1e-6));
    check(!geo_vector_equal(geo_vector(.x = -.1), geo_vector(.x = -.1, .w = .1), 1e-6));
  }

  it("sums all components when adding") {
    check(geo_vector_equal(
        geo_vector_add(
            geo_vector(.x = 1, .y = -2.1, .z = 3, .w = 4),
            geo_vector(.x = 2, .y = 3.2, .z = 4, .w = 5)),
        geo_vector(.x = 3, .y = 1.1, .z = 7, .w = 9),
        1e-6));

    check(geo_vector_equal(
        geo_vector_add(geo_vector(.x = 1, .y = 2, .z = 3), geo_vector(.x = 4, .y = 5, .z = 6)),
        geo_vector(.x = 5, .y = 7, .z = 9),
        1e-6));
  }

  it("subtracts all components when subtracting") {
    check(geo_vector_equal(
        geo_vector_sub(
            geo_vector(.x = 5, .y = -2.1, .z = 6, .w = 8),
            geo_vector(.x = 2, .y = 3.2, .z = 4, .w = 5)),
        geo_vector(.x = 3, .y = -5.3, .z = 2, .w = 3),
        1e-6));

    check(geo_vector_equal(
        geo_vector_sub(geo_vector(.x = 1, .y = 2, .z = 3), geo_vector(.x = 4, .y = 5, .z = 6)),
        geo_vector(.x = -3, .y = -3, .z = -3),
        1e-6));
  }

  it("multiplies each component by the scalar when mutliplying") {
    check(geo_vector_equal(
        geo_vector_mul(geo_vector(.x = 5, .y = -2.1, .z = 6, .w = 8), 2),
        geo_vector(.x = 10, .y = -4.2, .z = 12, .w = 16),
        1e-6));

    check(geo_vector_equal(
        geo_vector_mul(geo_vector(.x = 1, .y = 2, .z = 3), -2),
        geo_vector(.x = -2, .y = -4, .z = -6),
        1e-6));
  }

  it("divides each component by the scalar when dividing") {
    check(geo_vector_equal(
        geo_vector_div(geo_vector(.x = 5, .y = -2.1, .z = 6, .w = 8), 2),
        geo_vector(.x = 2.5, .y = -1.05, .z = 3, .w = 4),
        1e-6));

    check(geo_vector_equal(
        geo_vector_div(geo_vector(.x = 1, .y = 2, .z = 3), -2),
        geo_vector(.x = -.5, .y = -1, .z = -1.5),
        1e-6));
  }

  it("sums all the squared components when calculating the squared magnitude") {
    check_eq_float(geo_vector_mag_sqr(geo_vector(.x = 1, .y = 2, .z = 3, .w = 4)), 30, 1e-6);
    check_eq_float(geo_vector_mag_sqr(geo_vector(.x = 1, .y = 2, .z = 3)), 14, 1e-6);
  }

  it("computes the square-root of the squared components when calculating magnitude") {
    check_eq_float(geo_vector_mag(geo_vector()), 0, 1e-6);
    check_eq_float(geo_vector_mag(geo_vector(.y = 42)), 42, 1e-6);
  }

  it("returns a unit-vector when normalizing") {
    check(geo_vector_equal(geo_vector_norm(geo_up), geo_up, 1e-6));
    check(geo_vector_equal(geo_vector_norm(geo_vector(.y = 42)), geo_up, 1e-6));
    check_eq_float(
        geo_vector_mag(geo_vector_norm(geo_vector(.x = .1337, .y = 42, .w = -42))), 1, 1e-6);
  }

  it("returns 0 as the dot product of perpendicular unit vectors") {
    check_eq_float(geo_vector_dot(geo_up, geo_right), 0, 1e-6);
    check_eq_float(geo_vector_dot(geo_right, geo_forward), 0, 1e-6);
  }

  it("returns 1 as the dot product of equal unit vectors") {
    check_eq_float(geo_vector_dot(geo_up, geo_up), 1, 1e-6);
  }

  it("returns -1 as the dot product of opposite unit vectors") {
    check_eq_float(geo_vector_dot(geo_up, geo_down), -1, 1e-6);
    check_eq_float(geo_vector_dot(geo_right, geo_left), -1, 1e-6);
  }

  it("returns the cosine of the angle between unit-vectors when calculating the dot product") {
    const GeoVector a = {.y = 1};
    const GeoVector b = geo_vector_norm(geo_vector(.x = 1, .y = 1));

    check_eq_float(math_asin_f32(geo_vector_dot(a, b)) * math_rad_to_deg, 45, 1e-5);
  }

  it("returns forward as the cross product of right and up") {
    check(geo_vector_equal(geo_vector_cross3(geo_right, geo_up), geo_forward, 1e-6));
  }

  it("returns backward as the cross product of up and right") {
    check(geo_vector_equal(geo_vector_cross3(geo_right, geo_up), geo_forward, 1e-6));
  }

  it("returns 0 radians as the angle between parallel vectors") {
    check_eq_float(geo_vector_angle(geo_up, geo_up), 0, 1e-5);
    check_eq_float(geo_vector_angle(geo_up, geo_vector(.y = 42)), 0, 1e-5);
  }

  it("returns pi radians as the angle between opposite vectors") {
    check_eq_float(geo_vector_angle(geo_up, geo_down), math_pi, 1e-5);
    check_eq_float(geo_vector_angle(geo_down, geo_vector(.y = 42)), math_pi, 1e-5);
  }

  it("returns half pi radians as the angle between perpendicular vectors") {
    check_eq_float(geo_vector_angle(geo_up, geo_right), math_pi * .5, 1e-5);
    check_eq_float(geo_vector_angle(geo_backward, geo_left), math_pi * .5, 1e-5);
  }

  it("returns the same vector when projecting a vector onto itself") {
    const GeoVector v = {.x = -1, .y = 1, .z = 42};
    check(geo_vector_equal(geo_vector_project(v, v), v, 1e-6));
  }

  it("returns a zero vector when projecting a zero vector") {
    check(geo_vector_equal(geo_vector_project(geo_vector(), geo_forward), geo_vector(), 1e-6));
  }

  it("returns a zero vector when projecting a vector onto a zero vector") {
    const GeoVector v = {.x = -1, .y = 1, .z = 42};
    check(geo_vector_equal(geo_vector_project(v, geo_vector()), geo_vector(), 1e-6));
  }

  it("returns the overlap when projecting a vector onto another") {
    const GeoVector v1 = {.x = 3, .y = 3};
    const GeoVector v2 = {.x = 0, .y = 10};
    check(geo_vector_equal(geo_vector_project(v1, v2), geo_vector(.x = 0, .y = 3), 1e-6));
  }

  it("returns a zero vector when reflecting a zero vector") {
    check(geo_vector_equal(geo_vector_reflect(geo_vector(), geo_up), geo_vector(), 1e-6));
  }

  it("returns the same vector when reflecting a vector onto a zero vector") {
    const GeoVector v = {.x = 3, .y = 42};
    check(geo_vector_equal(geo_vector_reflect(v, geo_vector()), v, 1e-6));
  }

  it("returns a reverse vector when reflecting a vector onto an opposite normal") {
    const GeoVector v1 = {.x = 5, .y = 1};
    const GeoVector v2 = {.x = -1, .y = 0};
    check(geo_vector_equal(geo_vector_reflect(v1, v2), geo_vector(.x = -5, .y = 1), 1e-6));
  }

  it("can linearly interpolate vectors") {
    const GeoVector v1 = {.x = 10, .y = 20, .z = 10};
    const GeoVector v2 = {.x = 20, .y = 40, .z = 20};
    const GeoVector v3 = {.x = 15, .y = 30, .z = 15};
    check(geo_vector_equal(geo_vector_lerp(v1, v2, .5), v3, 1e-6));
  }

  it("divides each component by w when performing a perspective divide") {
    const GeoVector v1 = {.x = 1, .y = 2, .z = 4, .w = 4};
    const GeoVector v2 = {.x = .25, .y = .5, .z = 1};
    check(geo_vector_equal(geo_vector_perspective_div(v1), v2, 1e-6));
  }

  it("lists all components when formatted") {
    check_eq_string(fmt_write_scratch("{}", geo_vector_fmt(geo_forward)), string_lit("0, 0, 1, 0"));
    check_eq_string(fmt_write_scratch("{}", geo_vector_fmt(geo_up)), string_lit("0, 1, 0, 0"));
    check_eq_string(
        fmt_write_scratch("{}", geo_vector_fmt(geo_vector(.x = 42, .y = 1337, .z = 1, .w = 0.42))),
        string_lit("42, 1337, 1, 0.42"));
  }
}
