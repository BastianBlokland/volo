#include "check_spec.h"
#include "core_math.h"
#include "geo_quat.h"

#include "utils_internal.h"

spec(quat) {

  it("returns an identity quaternion when multiplying two identity quaternions") {
    check_eq_quat(geo_quat_mul(geo_quat_ident, geo_quat_ident), geo_quat_ident);
  }

  it("returns an identity quaternion when computing the inverse of a identity quaternion") {
    check_eq_quat(geo_quat_inverse(geo_quat_ident), geo_quat_ident);
  }

  it("returns the same vector when rotating by an identity quaternion") {
    const GeoVector v1 = {1, -2, 3};
    check_eq_vector(geo_quat_rotate(geo_quat_ident, v1), v1);
  }

  it("returns the difference quaternion when computing a from-to rotation") {
    const GeoQuat q1 = geo_quat_angle_axis(geo_right, 42);
    const GeoQuat q2 = geo_quat_angle_axis(geo_up, -42);

    check_eq_quat(geo_quat_from_to(geo_quat_ident, q1), q1);
    check_eq_quat(geo_quat_from_to(q1, q2), geo_quat_mul(q2, geo_quat_angle_axis(geo_left, 42)));
  }

  it("can compute the angle of a quaternion") {
    check_eq_float(geo_quat_angle(geo_quat_ident), 0, 1e-6);
    check_eq_float(geo_quat_angle(geo_quat_angle_axis(geo_up, math_pi_f32)), math_pi_f32, 1e-6);
    check_eq_float(
        geo_quat_angle(geo_quat_angle_axis(geo_right, math_pi_f32 * .42f)),
        math_pi_f32 * .42f,
        1e-6);
  }

  it("can combine quaternions") {
    const GeoQuat q1    = geo_quat_angle_axis(geo_up, 42);
    const GeoQuat q2    = geo_quat_angle_axis(geo_right, 13.37f);
    const GeoQuat comb1 = geo_quat_mul(q1, q2);
    const GeoQuat comb2 = geo_quat_mul(q2, q1);

    const GeoVector v = {.42f, 13.37f, -42};
    check_eq_vector(geo_quat_rotate(comb1, v), geo_quat_rotate(q1, geo_quat_rotate(q2, v)));
    check_eq_vector(geo_quat_rotate(comb2, v), geo_quat_rotate(q2, geo_quat_rotate(q1, v)));
  }

  it("can rotate vectors 180 degrees over y") {
    const GeoQuat q = geo_quat_angle_axis(geo_up, 180 * math_deg_to_rad);
    check_eq_vector(geo_quat_rotate(q, geo_left), geo_right);
  }

  it("can rotate vectors 90 degrees over y") {
    const GeoQuat q = geo_quat_angle_axis(geo_up, 90 * math_deg_to_rad);
    check_eq_vector(geo_quat_rotate(q, geo_left), geo_forward);
  }

  it("can rotate vectors by the inverse of 90 degrees over y") {
    const GeoQuat q = geo_quat_inverse(geo_quat_angle_axis(geo_up, 90 * math_deg_to_rad));
    check_eq_vector(geo_quat_rotate(q, geo_left), geo_backward);
  }

  it("can rotate vectors by arbitrary degrees") {
    const GeoQuat q1 = geo_quat_angle_axis(geo_up, 42.42f * math_deg_to_rad);
    check_eq_float(
        geo_vector_angle(geo_forward, geo_quat_rotate(q1, geo_forward)) * math_rad_to_deg,
        42.42,
        1e-5);

    const GeoQuat q2 = geo_quat_inverse(q1);
    check_eq_float(
        geo_vector_angle(geo_forward, geo_quat_rotate(q2, geo_forward)) * math_rad_to_deg,
        42.42,
        1e-5);
  }

  it("can normalize a quaternion") {
    const GeoQuat q  = {1337, 42, -42, 5};
    const GeoQuat qn = geo_quat_norm(q);

    check_eq_float(geo_vector_mag(geo_vector(qn.x, qn.y, qn.z, qn.w)), 1, 1e-6);
  }

  it("can create a quaternion to rotate to the given axis system") {
    {
      const GeoVector newForward = geo_vector_norm(geo_vector(.42f, 13.37f, -42));
      const GeoQuat   q          = geo_quat_look(newForward, geo_up);
      check_eq_vector(geo_quat_rotate(q, geo_forward), newForward);
    }
    {
      const GeoQuat   rotQuat = geo_quat_look(geo_right, geo_down);
      const GeoMatrix rotMat  = geo_matrix_rotate(geo_forward, geo_down, geo_right);
      const GeoVector vec1    = geo_matrix_transform(&rotMat, geo_vector(.42f, 13.37f, -42));
      const GeoVector vec2    = geo_quat_rotate(rotQuat, geo_vector(.42f, 13.37f, -42));
      check_eq_vector(vec1, vec2);
    }
  }

  it("can spherically interpolate between two values") {
    {
      const GeoQuat q1 = geo_quat_angle_axis(geo_right, 2.0f);
      const GeoQuat q2 = geo_quat_angle_axis(geo_right, 1.0f);
      check_eq_quat(geo_quat_slerp(q1, q2, 0.0f), geo_quat_angle_axis(geo_right, 2.0f));
      check_eq_quat(geo_quat_slerp(q1, q2, 0.5f), geo_quat_angle_axis(geo_right, 1.5f));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.0f), geo_quat_angle_axis(geo_right, 1.0f));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.5f), geo_quat_angle_axis(geo_right, 0.5f));
    }
    {
      const GeoQuat q1 = geo_quat_angle_axis(geo_up, 2.0f);
      const GeoQuat q2 = geo_quat_angle_axis(geo_up, 1.0f);
      check_eq_quat(geo_quat_slerp(q1, q2, 0.0f), geo_quat_angle_axis(geo_up, 2.0f));
      check_eq_quat(geo_quat_slerp(q1, q2, 0.5f), geo_quat_angle_axis(geo_up, 1.5f));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.0f), geo_quat_angle_axis(geo_up, 1.0f));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.5f), geo_quat_angle_axis(geo_up, 0.5f));
    }
    {
      const GeoQuat q1 = geo_quat_angle_axis(geo_forward, 2.0f);
      const GeoQuat q2 = geo_quat_angle_axis(geo_forward, 1.0f);
      check_eq_quat(geo_quat_slerp(q1, q2, 0.0f), geo_quat_angle_axis(geo_forward, 2.0f));
      check_eq_quat(geo_quat_slerp(q1, q2, 0.5f), geo_quat_angle_axis(geo_forward, 1.5f));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.0f), geo_quat_angle_axis(geo_forward, 1.0f));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.5f), geo_quat_angle_axis(geo_forward, 0.5f));
    }
    {
      const GeoQuat q1 = geo_quat_look(geo_forward, geo_up);
      const GeoQuat q2 = geo_quat_look(geo_backward, geo_up);
      check_eq_quat(geo_quat_slerp(q1, q2, 0.5f), geo_quat_look(geo_right, geo_up));
    }
    {
      const GeoQuat q1 = geo_quat_look(geo_forward, geo_up);
      const GeoQuat q2 = geo_quat_look(geo_forward, geo_up);
      check_eq_quat(geo_quat_slerp(q1, q2, 0.5f), geo_quat_look(geo_forward, geo_up));
    }
  }

  it("lists all components when formatted") {
    check_eq_string(
        fmt_write_scratch("{}", geo_quat_fmt(geo_quat_ident)), string_lit("0, 0, 0, 1"));
    check_eq_string(
        fmt_write_scratch("{}", geo_quat_fmt(((GeoQuat){1, 2, 3, 4}))), string_lit("1, 2, 3, 4"));
  }

  it("can be created from euler angles") {
    check_eq_quat(geo_quat_from_euler(geo_vector(0, 0, 0)), geo_quat_ident);
    check_eq_quat(
        geo_quat_from_euler(geo_vector(0.42f, 0, 0)), geo_quat_angle_axis(geo_right, 0.42f));
    check_eq_quat(geo_quat_from_euler(geo_vector(0, 0.42f, 0)), geo_quat_angle_axis(geo_up, 0.42f));
    check_eq_quat(
        geo_quat_from_euler(geo_vector(0, 0, 0.42f)), geo_quat_angle_axis(geo_forward, 0.42f));
    check_eq_quat(
        geo_quat_from_euler(geo_vector(0.1337f, 0, 0.42f)),
        geo_quat_mul(
            geo_quat_angle_axis(geo_forward, 0.42f), geo_quat_angle_axis(geo_right, 0.1337f)));
  }

  it("can be converted to euler angles") {
    check_eq_vector(geo_quat_to_euler(geo_quat_ident), geo_vector(0, 0, 0));
    check_eq_vector(
        geo_quat_to_euler(geo_quat_angle_axis(geo_right, 0.42f)), geo_vector(0.42f, 0, 0));
    check_eq_vector(geo_quat_to_euler(geo_quat_angle_axis(geo_up, 0.42f)), geo_vector(0, 0.42f, 0));
    check_eq_vector(
        geo_quat_to_euler(geo_quat_angle_axis(geo_forward, 0.42f)), geo_vector(0, 0, 0.42f));
    check_eq_vector(
        geo_quat_to_euler(geo_quat_mul(
            geo_quat_angle_axis(geo_forward, 0.42f), geo_quat_angle_axis(geo_right, 0.1337f))),
        geo_vector(0.1337f, 0, 0.42f));
  }

  it("roundtrips the euler conversion") {
    const GeoQuat   q1 = geo_quat_from_euler(geo_vector(0.1337f, 1.2345f, 0.42f));
    const GeoVector e  = geo_quat_to_euler(q1);
    const GeoQuat   q2 = geo_quat_from_euler(e);
    check_eq_quat(q1, q2);
  }
}
