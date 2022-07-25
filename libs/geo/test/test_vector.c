#include "check_spec.h"

#include "utils_internal.h"

spec(vector) {

  it("initializes non specified components to 0") {
    check_eq_vector(geo_vector(0), ((GeoVector){0, 0, 0, 0}));
    check_eq_vector(geo_vector(1), ((GeoVector){1, 0, 0, 0}));
    check_eq_vector(geo_vector(1, 2), ((GeoVector){1, 2, 0, 0}));
    check_eq_vector(geo_vector(1, 2, 3), ((GeoVector){1, 2, 3, 0}));
    check_eq_vector(geo_vector(1, 2, 3, 4), ((GeoVector){1, 2, 3, 4}));
    check_eq_vector(geo_vector(.y = 1), ((GeoVector){0, 1, 0, 0}));
    check_eq_vector(geo_vector(.w = -42, .z = 2), ((GeoVector){0, 0, 2, -42}));
  }

  it("compares the magnitude of the difference vector to a threshold when equated") {
    check_eq_vector(geo_forward, geo_forward);
    check_eq_vector(geo_forward, geo_forward);
    check(!geo_vector_equal(geo_forward, geo_backward, 1e-6f));
    check(!geo_vector_equal(geo_vector(.x = -.1f), geo_vector(.x = -.1f, .w = .1f), 1e-6f));
  }

  it("can compute the absolute value of each component") {
    check_eq_vector(geo_vector_abs(geo_vector(0, 0, 0, 0)), geo_vector(0, 0, 0, 0));
    check_eq_vector(geo_vector_abs(geo_vector(1, 1, 1, 1)), geo_vector(1, 1, 1, 1));
    check_eq_vector(geo_vector_abs(geo_vector(-1, -1, -1, -1)), geo_vector(1, 1, 1, 1));
    check_eq_vector(
        geo_vector_abs(geo_vector(-0.0f, -0.001f, 42, -1337)), geo_vector(0, 0.001f, 42, 1337));
  }

  it("sums all components when adding") {
    check_eq_vector(
        geo_vector_add(
            geo_vector(.x = 1, .y = -2.1f, .z = 3, .w = 4),
            geo_vector(.x = 2, .y = 3.2f, .z = 4, .w = 5)),
        geo_vector(.x = 3, .y = 1.1f, .z = 7, .w = 9));

    check_eq_vector(
        geo_vector_add(geo_vector(.x = 1, .y = 2, .z = 3), geo_vector(.x = 4, .y = 5, .z = 6)),
        geo_vector(.x = 5, .y = 7, .z = 9));
  }

  it("subtracts all components when subtracting") {
    check_eq_vector(
        geo_vector_sub(
            geo_vector(.x = 5, .y = -2.1f, .z = 6, .w = 8),
            geo_vector(.x = 2, .y = 3.2f, .z = 4, .w = 5)),
        geo_vector(.x = 3, .y = -5.3f, .z = 2, .w = 3));

    check_eq_vector(
        geo_vector_sub(geo_vector(.x = 1, .y = 2, .z = 3), geo_vector(.x = 4, .y = 5, .z = 6)),
        geo_vector(.x = -3, .y = -3, .z = -3));
  }

  it("multiplies each component by the scalar when mutliplying") {
    check_eq_vector(
        geo_vector_mul(geo_vector(.x = 5, .y = -2.1f, .z = 6, .w = 8), 2),
        geo_vector(.x = 10, .y = -4.2f, .z = 12, .w = 16));

    check_eq_vector(
        geo_vector_mul(geo_vector(.x = 1, .y = 2, .z = 3), -2),
        geo_vector(.x = -2, .y = -4, .z = -6));
  }

  it("multiplies each component when mutliplying component-wise") {
    const GeoVector v1 = {.x = 10, .y = 20, .z = 10, .w = 2};
    const GeoVector v2 = {.x = 2, .y = 3, .z = -4, .w = 0};
    check_eq_vector(geo_vector_mul_comps(v1, v2), geo_vector(.x = 20, .y = 60, .z = -40, .w = 0));
  }

  it("divides each component by the scalar when dividing") {
    check_eq_vector(
        geo_vector_div(geo_vector(.x = 5, .y = -2.1f, .z = 6, .w = 8), 2),
        geo_vector(.x = 2.5, .y = -1.05f, .z = 3, .w = 4));

    check_eq_vector(
        geo_vector_div(geo_vector(.x = 1, .y = 2, .z = 3), -2),
        geo_vector(.x = -.5, .y = -1, .z = -1.5));
  }

  it("multiplies each component when dividing component-wise") {
    const GeoVector v1 = {.x = 20, .y = 60, .z = 10, .w = 2};
    const GeoVector v2 = {.x = 2, .y = 3, .z = -4, .w = 1};
    check_eq_vector(geo_vector_div_comps(v1, v2), geo_vector(.x = 10, .y = 20, .z = -2.5f, .w = 2));
  }

  it("sums all the squared components when calculating the squared magnitude") {
    check_eq_float(geo_vector_mag_sqr(geo_vector(.x = 1, .y = 2, .z = 3, .w = 4)), 30, 1e-6f);
    check_eq_float(geo_vector_mag_sqr(geo_vector(.x = 1, .y = 2, .z = 3)), 14, 1e-6f);
  }

  it("computes the square-root of the squared components when calculating magnitude") {
    check_eq_float(geo_vector_mag(geo_vector(0)), 0, 1e-6f);
    check_eq_float(geo_vector_mag(geo_vector(.y = 42)), 42, 1e-6f);
  }

  it("returns a unit-vector when normalizing") {
    check_eq_vector(geo_vector_norm(geo_up), geo_up);
    check_eq_vector(geo_vector_norm(geo_vector(.y = 42)), geo_up);
    check_eq_float(
        geo_vector_mag(geo_vector_norm(geo_vector(.x = .1337f, .y = 42, .w = -42))), 1, 1e-6f);
  }

  it("returns 0 as the dot product of perpendicular unit vectors") {
    check_eq_float(geo_vector_dot(geo_up, geo_right), 0, 1e-6f);
    check_eq_float(geo_vector_dot(geo_right, geo_forward), 0, 1e-6f);
  }

  it("returns 1 as the dot product of equal unit vectors") {
    check_eq_float(geo_vector_dot(geo_up, geo_up), 1, 1e-6f);
  }

  it("returns -1 as the dot product of opposite unit vectors") {
    check_eq_float(geo_vector_dot(geo_up, geo_down), -1, 1e-6f);
    check_eq_float(geo_vector_dot(geo_right, geo_left), -1, 1e-6f);
  }

  it("returns the cosine of the angle between unit-vectors when calculating the dot product") {
    const GeoVector a = {.y = 1};
    const GeoVector b = geo_vector_norm(geo_vector(.x = 1, .y = 1));

    check_eq_float(math_asin_f32(geo_vector_dot(a, b)) * math_rad_to_deg, 45, 1e-5);
  }

  it("returns forward as the cross product of right and up") {
    check_eq_vector(geo_vector_cross3(geo_right, geo_up), geo_forward);
  }

  it("returns backward as the cross product of up and right") {
    check_eq_vector(geo_vector_cross3(geo_right, geo_up), geo_forward);
  }

  it("returns 0 radians as the angle between parallel vectors") {
    check_eq_float(geo_vector_angle(geo_up, geo_up), 0, 1e-5);
    check_eq_float(geo_vector_angle(geo_up, geo_vector(.y = 42)), 0, 1e-5);
  }

  it("returns pi radians as the angle between opposite vectors") {
    check_eq_float(geo_vector_angle(geo_up, geo_down), math_pi_f32, 1e-5);
    check_eq_float(geo_vector_angle(geo_down, geo_vector(.y = 42)), math_pi_f32, 1e-5);
  }

  it("returns half pi radians as the angle between perpendicular vectors") {
    check_eq_float(geo_vector_angle(geo_up, geo_right), math_pi_f32 * .5, 1e-5);
    check_eq_float(geo_vector_angle(geo_backward, geo_left), math_pi_f32 * .5, 1e-5);
  }

  it("returns the same vector when projecting a vector onto itself") {
    const GeoVector v = {.x = -1, .y = 1, .z = 42};
    check_eq_vector(geo_vector_project(v, v), v);
  }

  it("returns a zero vector when projecting a zero vector") {
    check_eq_vector(geo_vector_project(geo_vector(0), geo_forward), geo_vector(0));
  }

  it("returns a zero vector when projecting a vector onto a zero vector") {
    const GeoVector v = {.x = -1, .y = 1, .z = 42};
    check_eq_vector(geo_vector_project(v, geo_vector(0)), geo_vector(0));
  }

  it("returns the overlap when projecting a vector onto another") {
    const GeoVector v1 = {.x = 3, .y = 3};
    const GeoVector v2 = {.x = 0, .y = 10};
    check_eq_vector(geo_vector_project(v1, v2), geo_vector(.x = 0, .y = 3));
  }

  it("returns a zero vector when reflecting a zero vector") {
    check_eq_vector(geo_vector_reflect(geo_vector(0), geo_up), geo_vector(0));
  }

  it("returns the same vector when reflecting a vector onto a zero vector") {
    const GeoVector v = {.x = 3, .y = 42};
    check_eq_vector(geo_vector_reflect(v, geo_vector(0)), v);
  }

  it("returns a reverse vector when reflecting a vector onto an opposite normal") {
    const GeoVector v1 = {.x = 5, .y = 1};
    const GeoVector v2 = {.x = -1, .y = 0};
    check_eq_vector(geo_vector_reflect(v1, v2), geo_vector(.x = -5, .y = 1));
  }

  it("can linearly interpolate vectors") {
    const GeoVector v1 = {.x = 10, .y = 20, .z = 10};
    const GeoVector v2 = {.x = 20, .y = 40, .z = 20};
    const GeoVector v3 = {.x = 15, .y = 30, .z = 15};
    check_eq_vector(geo_vector_lerp(v1, v2, .5), v3);
  }

  it("can compute the minimum value of each component") {
    const GeoVector v1 = {.x = 2, .y = 6, .z = -5, .w = 5};
    const GeoVector v2 = {.x = 4, .y = -2, .z = 6, .w = 5};
    check_eq_vector(geo_vector_min(v1, v2), geo_vector(2, -2, -5, 5));
  }

  it("can compute the maximum value of each component") {
    const GeoVector v1 = {.x = 2, .y = 6, .z = -5, .w = 5};
    const GeoVector v2 = {.x = 4, .y = -2, .z = 6, .w = 5};
    check_eq_vector(geo_vector_max(v1, v2), geo_vector(4, 6, 6, 5));
  }

  it("can compute the square root of components") {
    const GeoVector v = {.x = 16, .y = 64, .z = 256};
    check_eq_vector(geo_vector_sqrt(v), geo_vector(4, 8, 16));
  }

  it("can clamp its magnitude") {
    check_eq_vector(geo_vector_clamp(geo_vector(1, 2, 3), 10), geo_vector(1, 2, 3));
    check_eq_vector(geo_vector_clamp(geo_vector(34, 0, 0), 2), geo_vector(2, 0, 0));
    check_eq_vector(geo_vector_clamp(geo_vector(1, 2, 3), 0), geo_vector(0, 0, 0));
    check_eq_vector(geo_vector_clamp(geo_vector(0, 0, 0), 0), geo_vector(0, 0, 0));
  }

  it("divides each component by w when performing a perspective divide") {
    const GeoVector v1 = {.x = 1, .y = 2, .z = 4, .w = 4};
    const GeoVector v2 = {.x = .25, .y = .5, .z = 1};
    check_eq_vector(geo_vector_perspective_div(v1), v2);
  }

  it("lists all components when formatted") {
    check_eq_string(fmt_write_scratch("{}", geo_vector_fmt(geo_forward)), string_lit("0, 0, 1, 0"));
    check_eq_string(fmt_write_scratch("{}", geo_vector_fmt(geo_up)), string_lit("0, 1, 0, 0"));
    check_eq_string(
        fmt_write_scratch("{}", geo_vector_fmt(geo_vector(.x = 42, .y = 1337, .z = 1, .w = 0.42f))),
        string_lit("42, 1337, 1, 0.42"));
  }
}
