#include "check_spec.h"
#include "core_array.h"
#include "core_float.h"
#include "core_math.h"
#include "geo_matrix.h"
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

  it("has preset quaternions for common rotations") {
    {
      check_eq_vector(geo_quat_rotate(geo_quat_forward_to_right, geo_forward), geo_right);
      check_eq_quat(geo_quat_forward_to_right, geo_quat_angle_axis(math_pi_f32 * 0.5f, geo_up));
    }
    {
      check_eq_vector(geo_quat_rotate(geo_quat_forward_to_left, geo_forward), geo_left);
      check_eq_quat(geo_quat_forward_to_left, geo_quat_angle_axis(math_pi_f32 * -0.5f, geo_up));
    }
    {
      check_eq_vector(geo_quat_rotate(geo_quat_forward_to_up, geo_forward), geo_up);
      check_eq_quat(geo_quat_forward_to_up, geo_quat_angle_axis(math_pi_f32 * -0.5f, geo_right));
    }
    {
      check_eq_vector(geo_quat_rotate(geo_quat_forward_to_down, geo_forward), geo_down);
      check_eq_quat(geo_quat_forward_to_down, geo_quat_angle_axis(math_pi_f32 * 0.5f, geo_right));
    }
    {
      check_eq_vector(geo_quat_rotate(geo_quat_forward_to_forward, geo_forward), geo_forward);
      check_eq_quat(geo_quat_forward_to_forward, geo_quat_ident);
    }
    {
      check_eq_vector(geo_quat_rotate(geo_quat_forward_to_backward, geo_forward), geo_backward);
      check_eq_quat(geo_quat_forward_to_backward, geo_quat_angle_axis(math_pi_f32, geo_up));
    }
    {
      check_eq_vector(geo_quat_rotate(geo_quat_up_to_forward, geo_up), geo_forward);
      check_eq_quat(geo_quat_up_to_forward, geo_quat_angle_axis(math_pi_f32 * 0.5f, geo_right));
    }
  }

  it("returns the difference quaternion when computing a from-to rotation") {
    const GeoQuat q1 = geo_quat_angle_axis(42, geo_right);
    const GeoQuat q2 = geo_quat_angle_axis(-42, geo_up);

    check_eq_quat(geo_quat_from_to(geo_quat_ident, q1), q1);
    check_eq_quat(geo_quat_from_to(q1, q2), geo_quat_mul(q2, geo_quat_angle_axis(42, geo_left)));
  }

  it("can combine quaternions") {
    const GeoQuat q1    = geo_quat_angle_axis(42, geo_up);
    const GeoQuat q2    = geo_quat_angle_axis(13.37f, geo_right);
    const GeoQuat comb1 = geo_quat_mul(q1, q2);
    const GeoQuat comb2 = geo_quat_mul(q2, q1);

    const GeoVector v = {.42f, 13.37f, -42};
    check_eq_vector(geo_quat_rotate(comb1, v), geo_quat_rotate(q1, geo_quat_rotate(q2, v)));
    check_eq_vector(geo_quat_rotate(comb2, v), geo_quat_rotate(q2, geo_quat_rotate(q1, v)));
  }

  it("can rotate vectors 180 degrees over y") {
    const GeoQuat q = geo_quat_angle_axis(180 * math_deg_to_rad, geo_up);
    check_eq_vector(geo_quat_rotate(q, geo_left), geo_right);
  }

  it("can rotate vectors 90 degrees over y") {
    const GeoQuat q = geo_quat_angle_axis(90 * math_deg_to_rad, geo_up);
    check_eq_vector(geo_quat_rotate(q, geo_left), geo_forward);
  }

  it("can rotate vectors by the inverse of 90 degrees over y") {
    const GeoQuat q = geo_quat_inverse(geo_quat_angle_axis(90 * math_deg_to_rad, geo_up));
    check_eq_vector(geo_quat_rotate(q, geo_left), geo_backward);
  }

  it("can rotate vectors by arbitrary degrees") {
    const GeoQuat q1 = geo_quat_angle_axis(42.42f * math_deg_to_rad, geo_up);
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

  it("can normalize a quaternion (even if zero length)") {
    {
      const GeoQuat q  = {0};
      const GeoQuat qn = geo_quat_norm_or_ident(q);

      check_eq_quat(qn, geo_quat_ident);
      check_eq_float(geo_vector_mag(geo_vector(qn.x, qn.y, qn.z, qn.w)), 1, 1e-6);
    }
    {
      const GeoQuat q  = {1337, 42, -42, 5};
      const GeoQuat qn = geo_quat_norm_or_ident(q);

      check_eq_float(geo_vector_mag(geo_vector(qn.x, qn.y, qn.z, qn.w)), 1, 1e-6);
    }
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
      const GeoQuat q1 = geo_quat_angle_axis(2.0f, geo_right);
      const GeoQuat q2 = geo_quat_angle_axis(1.0f, geo_right);
      check_eq_quat(geo_quat_slerp(q1, q2, 0.0f), geo_quat_angle_axis(2.0f, geo_right));
      check_eq_quat(geo_quat_slerp(q1, q2, 0.5f), geo_quat_angle_axis(1.5f, geo_right));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.0f), geo_quat_angle_axis(1.0f, geo_right));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.5f), geo_quat_angle_axis(0.5f, geo_right));
    }
    {
      const GeoQuat q1 = geo_quat_angle_axis(2.0f, geo_up);
      const GeoQuat q2 = geo_quat_angle_axis(1.0f, geo_up);
      check_eq_quat(geo_quat_slerp(q1, q2, 0.0f), geo_quat_angle_axis(2.0f, geo_up));
      check_eq_quat(geo_quat_slerp(q1, q2, 0.5f), geo_quat_angle_axis(1.5f, geo_up));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.0f), geo_quat_angle_axis(1.0f, geo_up));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.5f), geo_quat_angle_axis(0.5f, geo_up));
    }
    {
      const GeoQuat q1 = geo_quat_angle_axis(2.0f, geo_forward);
      const GeoQuat q2 = geo_quat_angle_axis(1.0f, geo_forward);
      check_eq_quat(geo_quat_slerp(q1, q2, 0.0f), geo_quat_angle_axis(2.0f, geo_forward));
      check_eq_quat(geo_quat_slerp(q1, q2, 0.5f), geo_quat_angle_axis(1.5f, geo_forward));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.0f), geo_quat_angle_axis(1.0f, geo_forward));
      check_eq_quat(geo_quat_slerp(q1, q2, 1.5f), geo_quat_angle_axis(0.5f, geo_forward));
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

  it("can rotate towards a target rotation") {
    {
      GeoQuat q = geo_quat_angle_axis(1.0f, geo_forward);
      check(!geo_quat_towards(&q, geo_quat_angle_axis(1.5f, geo_forward), 0.1f));
      check_eq_quat(q, geo_quat_angle_axis(1.1f, geo_forward));
    }
    {
      GeoQuat q = geo_quat_angle_axis(1.0f, geo_forward);
      check(geo_quat_towards(&q, geo_quat_angle_axis(1.5f, geo_forward), 1.0f));
      check_eq_quat(q, geo_quat_angle_axis(1.5f, geo_forward));
    }
  }

  it("lists all components when formatted") {
    check_eq_string(
        fmt_write_scratch("{}", geo_quat_fmt(geo_quat_ident)), string_lit("0, 0, 0, 1"));
    check_eq_string(
        fmt_write_scratch("{}", geo_quat_fmt(((GeoQuat){1, 2, 3, 4}))), string_lit("1, 2, 3, 4"));
  }

  it("can be created from an angle-axis representation") {
    {
      const GeoQuat q = geo_quat_angle_axis(0.25f * math_pi_f32 * 2.0f, geo_up);
      check_eq_quat(q, ((GeoQuat){.x = 0.0f, .y = 0.7071068f, .z = 0.0f, .w = 0.7071068f}));
    }
    {
      const GeoQuat q = geo_quat_angle_axis(0.75f * math_pi_f32 * 2.0f, geo_up);
      check_eq_quat(q, ((GeoQuat){.x = 0.0f, .y = 0.7071068f, .z = 0.0f, .w = -0.7071068f}));
    }
  }

  it("can be created from euler angles") {
    check_eq_quat(geo_quat_from_euler(geo_vector(0, 0, 0)), geo_quat_ident);
    check_eq_quat(
        geo_quat_from_euler(geo_vector(0.42f, 0, 0)), geo_quat_angle_axis(0.42f, geo_right));
    check_eq_quat(geo_quat_from_euler(geo_vector(0, 0.42f, 0)), geo_quat_angle_axis(0.42f, geo_up));
    check_eq_quat(
        geo_quat_from_euler(geo_vector(0, 0, 0.42f)), geo_quat_angle_axis(0.42f, geo_forward));
    check_eq_quat(
        geo_quat_from_euler(geo_vector(0.1337f, 0, 0.42f)),
        geo_quat_mul(
            geo_quat_angle_axis(0.42f, geo_forward), geo_quat_angle_axis(0.1337f, geo_right)));
  }

  it("can be converted to euler angles") {
    check_eq_vector(geo_quat_to_euler(geo_quat_ident), geo_vector(0, 0, 0));
    check_eq_vector(
        geo_quat_to_euler(geo_quat_angle_axis(0.42f, geo_right)), geo_vector(0.42f, 0, 0));
    check_eq_vector(geo_quat_to_euler(geo_quat_angle_axis(0.42f, geo_up)), geo_vector(0, 0.42f, 0));
    check_eq_vector(
        geo_quat_to_euler(geo_quat_angle_axis(0.42f, geo_forward)), geo_vector(0, 0, 0.42f));
    check_eq_vector(
        geo_quat_to_euler(geo_quat_mul(
            geo_quat_angle_axis(0.42f, geo_forward), geo_quat_angle_axis(0.1337f, geo_right))),
        geo_vector(0.1337f, 0, 0.42f));
  }

  it("round-trips the euler conversion") {
    static const GeoVector g_testRotEulerDeg[] = {
        {.x = 133.7f, .y = 12.345f, .z = 42.0f},
        {.x = 180.0f, .y = 3.4981172085f, .z = 180.0f},
        {.x = -180.0f, .y = 3.4981172085f, .z = -180.0f},
    };
    array_for_t(g_testRotEulerDeg, GeoVector, rotEulerDeg) {
      const GeoVector rotEulerRad = geo_vector_mul(*rotEulerDeg, math_deg_to_rad);
      const GeoQuat   q1          = geo_quat_from_euler(rotEulerRad);
      const GeoVector e           = geo_quat_to_euler(q1);
      const GeoQuat   q2          = geo_quat_from_euler(e);
      check_eq_quat(q1, q2);
    }
  }

  it("can be converted to an angle-axis representation") {
    {
      const GeoVector aa = geo_quat_to_angle_axis(geo_quat_ident);
      check_eq_vector(aa, geo_vector(0, 0, 0));
    }
    {
      const GeoVector aa = geo_quat_to_angle_axis(geo_quat_angle_axis(math_pi_f32, geo_up));
      check_eq_vector(aa, geo_vector(0, math_pi_f32, 0));
    }
  }

  it("round-trips the angle-axis conversion") {
    const GeoVector orgAxis  = geo_vector_norm(geo_vector(-1, 2, 3));
    const f32       orgAngle = math_pi_f32 * 1.337f;

    const GeoQuat   q1    = geo_quat_angle_axis(orgAngle, orgAxis);
    const GeoVector aa    = geo_quat_to_angle_axis(q1);
    const f32       angle = geo_vector_mag(aa);
    const GeoVector axis  = geo_vector_div(aa, angle);

    check_eq_float(orgAngle, angle, 1e-6f);
    check_eq_vector(orgAxis, axis);

    const GeoQuat q2 = geo_quat_angle_axis(angle, axis);
    check_eq_quat(q1, q2);
  }

  it("can decompose into swing and twist") {
    {
      const GeoVector axis1 = geo_vector_norm(geo_vector(-1, 2, 3));
      const GeoVector axis2 = geo_vector_norm(geo_vector(-2, -2, 3));
      const f32       angle = math_pi_f32 * 1.337f;

      const GeoQuat       q1 = geo_quat_angle_axis(angle, axis1);
      const GeoSwingTwist st = geo_quat_to_swing_twist(q1, axis2);
      const GeoQuat       q2 = geo_quat_mul(st.swing, st.twist);

      check_eq_quat(q1, q2);
    }
    {
      const GeoQuat       q  = geo_quat_angle_axis(1.337f, geo_up);
      const GeoSwingTwist sw = geo_quat_to_swing_twist(q, geo_up);
      check_eq_quat(sw.swing, geo_quat_ident);
      check_eq_quat(sw.twist, q);
    }
    {
      const GeoQuat       q  = geo_quat_angle_axis(1.337f, geo_up);
      const GeoSwingTwist sw = geo_quat_to_swing_twist(q, geo_right);
      check_eq_quat(sw.swing, q);
      check_eq_quat(sw.twist, geo_quat_ident);
    }
    {
      const GeoQuat       q  = geo_quat_angle_axis(1.337f, geo_up);
      const GeoSwingTwist sw = geo_quat_to_swing_twist(q, geo_forward);
      check_eq_quat(sw.swing, q);
      check_eq_quat(sw.twist, geo_quat_ident);
    }
  }

  it("can decompose into twist") {
    {
      const GeoQuat q = geo_quat_angle_axis(1.337f, geo_up);
      check_eq_quat(geo_quat_to_twist(q, geo_up), q);
    }
    {
      const GeoQuat q = geo_quat_angle_axis(1.337f, geo_up);
      check_eq_quat(geo_quat_to_twist(q, geo_right), geo_quat_ident);
    }
    {
      const GeoQuat q = geo_quat_angle_axis(1.337f, geo_up);
      check_eq_quat(geo_quat_to_twist(q, geo_forward), geo_quat_ident);
    }
  }

  it("can clamp rotations") {
    {
      GeoQuat q = geo_quat_angle_axis(0.42f, geo_right);
      check(geo_quat_clamp(&q, 0.1f));
    }
    {
      GeoQuat q = geo_quat_angle_axis(0.42f, geo_right);
      check(!geo_quat_clamp(&q, 0.84f));
    }
    {
      GeoQuat q = geo_quat_angle_axis(0.42f, geo_right);
      geo_quat_clamp(&q, 0.1f);
      check_eq_quat(q, geo_quat_angle_axis(0.1f, geo_right));
    }
    {
      GeoQuat q = geo_quat_angle_axis(0.42f, geo_right);
      geo_quat_clamp(&q, 0.0f);
      check_eq_quat(q, geo_quat_ident);
    }
  }

  it("can be packed into 16 bits") {
    const GeoQuat q = geo_quat_from_euler(geo_vector(0.1337f, 13.37f, 0.42f));

    f16 packed[4];
    geo_quat_pack_f16(q, packed);

    check_eq_float(float_f16_to_f32(packed[0]), q.x, 1e-3f);
    check_eq_float(float_f16_to_f32(packed[1]), q.y, 1e-3f);
    check_eq_float(float_f16_to_f32(packed[2]), q.z, 1e-3f);
    check_eq_float(float_f16_to_f32(packed[3]), q.w, 1e-3f);
  }
}
