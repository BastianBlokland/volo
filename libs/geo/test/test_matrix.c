#include "check_spec.h"
#include "core_math.h"

#include "utils_internal.h"

spec(matrix) {

  it("returns an identity matrix when multiplying two identity matrices") {
    const GeoMatrix ident = geo_matrix_ident();
    check_eq_matrix(geo_matrix_mul(&ident, &ident), geo_matrix_ident());
  }

  it("returns the dot products of the rows and columns when multiplying two matrices") {
    {
      const GeoMatrix mA        = {.columns = {{1, 3}, {2, 4}}};
      const GeoMatrix mB        = {.columns = {{2, 1}, {0, 2}}};
      const GeoMatrix mExpected = {.columns = {{4, 10}, {4, 8}}};
      check_eq_matrix(geo_matrix_mul(&mA, &mB), mExpected);
    }
    {
      const GeoMatrix mA        = {.columns = {{2, 1}, {0, 2}}};
      const GeoMatrix mB        = {.columns = {{1, 3}, {2, 4}}};
      const GeoMatrix mExpected = {.columns = {{2, 7}, {4, 10}}};
      check_eq_matrix(geo_matrix_mul(&mA, &mB), mExpected);
    }
  }

  it("returns the dot products with the rows when transforming a vector") {
    const GeoMatrix m = {
        .columns = {
            {1, 0, 0},
            {-1, -3, 0},
            {2, 1, 1},
        }};
    check_eq_vector(geo_matrix_transform(&m, geo_vector(2, 1)), geo_vector(1, -3));
  }

  it("takes the 4th column into account for transform3 point") {
    const GeoMatrix m = {
        .columns = {
            {1, 0, 0, 0},
            {-1, -3, 0, 0},
            {2, 1, 1, 0},
            {1, 2, 3, 0},
        }};
    check_eq_vector(geo_matrix_transform3_point(&m, geo_vector(2, 1, 0)), geo_vector(2, -1, 3));
  }

  it("exchanges the rows and columns when transposing") {
    const GeoMatrix m = {.columns = {{1, 4, 7}, {2, 5, 8}, {3, 6, 9}}};
    const GeoMatrix t = {.columns = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}};
    check_eq_matrix(geo_matrix_transpose(&m), t);
    check_eq_matrix(geo_matrix_transpose(&t), m);
  }

  it("can invert orthogonal projection matrices") {
    const GeoMatrix m = geo_matrix_proj_ortho(10, 5, -2, 2);
    const GeoMatrix i = geo_matrix_inverse(&m);
    check_eq_vector(geo_matrix_transform(&i, geo_vector(0, 0, .5f, 1)), geo_vector(0, 0, 0, 1));
    check_eq_vector(geo_matrix_transform(&i, geo_vector(+1, 0, .5f, 1)), geo_vector(+5, 0, 0, 1));
    check_eq_vector(geo_matrix_transform(&i, geo_vector(-1, 0, .5f, 1)), geo_vector(-5, 0, 0, 1));
    check_eq_vector(geo_matrix_transform(&i, geo_vector(-1, -2, .5f, 1)), geo_vector(-5, 5, 0, 1));
    check_eq_vector(geo_matrix_transform(&i, geo_vector(-1, 2, .5f, 1)), geo_vector(-5, -5, 0, 1));
    check_eq_vector(geo_matrix_transform(&i, geo_vector(-1, 0, 1, 1)), geo_vector(-5, 0, -2, 1));
    check_eq_vector(geo_matrix_transform(&i, geo_vector(-1, 0, 0, 1)), geo_vector(-5, 0, +2, 1));
  }

  it("can invert perspective projection matrices") {
    const f32       fov = 90 * math_deg_to_rad;
    const GeoMatrix m   = geo_matrix_proj_pers(fov, fov, 0.42f);
    const GeoMatrix i   = geo_matrix_inverse(&m);

    // Reversed-z depth so, near plane is at depth 1.
    const GeoVector v1 = geo_matrix_transform(&i, geo_vector(0, 0, 1, 1));
    check_eq_vector(geo_vector_perspective_div(v1), geo_vector(0, 0, 0.42f));

    // Reversed-z depth with infinite far plane, so infinite z is at depth 0.
    const GeoVector v2 = geo_matrix_transform(&i, geo_vector(0, 0, 0.000001f, 1));
    check_eq_vector(geo_vector_perspective_div(v2), geo_vector(0, 0, 420000, 0));
  }

  it("roundtrips when inverting") {
    const GeoMatrix m1A = geo_matrix_rotate_x(math_pi_f32 * .25f);
    const GeoMatrix m1B = geo_matrix_scale(geo_vector(1, 2, 3));
    const GeoMatrix m1  = geo_matrix_mul(&m1A, &m1B);
    const GeoMatrix m2  = geo_matrix_inverse(&m1);
    const GeoMatrix m3  = geo_matrix_inverse(&m2);

    check_eq_matrix(m3, m1);
  }

  it("returns the same matrix when multiplying with the identity matrix") {
    const GeoMatrix mA = {
        .columns = {{1, 4, 7}, {2, 5, 8}, {3, 6, 9}},
    };
    const GeoMatrix mB = geo_matrix_ident();
    check_eq_matrix(geo_matrix_mul(&mA, &mB), mA);
  }

  it("returns the same vector when transforming with the identity matrix") {
    const GeoVector v = geo_vector(2, 3, 4);
    const GeoMatrix m = geo_matrix_ident();
    check_eq_vector(geo_matrix_transform(&m, v), v);
  }

  it("applies translation as an offset to position vectors") {
    const GeoMatrix m = geo_matrix_translate(geo_vector(-1, 2, .1f));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(0, 0, 0, 1)), geo_vector(-1, 2, .1f, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(1, 1, 1, 1)), geo_vector(0, 3, 1.1f, 1));
    check_eq_vector(
        geo_matrix_transform(&m, geo_vector(-1, -1, -1, 1)), geo_vector(-2, 1, -.9f, 1));
  }

  it("ignores translation for direction vectors") {
    const GeoMatrix m = geo_matrix_translate(geo_vector(-1, 2, .1f));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(0, 0, 0, 0)), geo_vector(0, 0, 0, 0));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(1, 1, 1, 0)), geo_vector(1, 1, 1, 0));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(-1, -1, -1, 0)), geo_vector(-1, -1, -1, 0));
  }

  it("applies scale as a multiplier to position and direction vectors") {
    const GeoMatrix m = geo_matrix_scale(geo_vector(1, 2, 3));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(0, 0, 0, 1)), geo_vector(0, 0, 0, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(1, 1, 1, 1)), geo_vector(1, 2, 3, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(2, 3, 4, 1)), geo_vector(2, 6, 12, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(2, 3, 4, 0)), geo_vector(2, 6, 12, 0));
  }

  it("can extract the translation vector") {
    const GeoVector vec = geo_vector(42.0f, -1337.0f, .1f);
    const GeoMatrix mT  = geo_matrix_translate(vec);
    const GeoMatrix mR  = geo_matrix_rotate_x(math_pi_f32 * .25f);
    const GeoMatrix mS  = geo_matrix_scale(geo_vector(1, 2, 3));
    const GeoMatrix m1  = geo_matrix_mul(&mT, &mR);
    const GeoMatrix m2  = geo_matrix_mul(&m1, &mS);

    check_eq_vector(geo_matrix_to_translation(&m2), vec);
  }

  it("can extract the scale vector") {
    const GeoVector scale = geo_vector(1.42f, 2.42f, 1.3337f);
    const GeoMatrix mT    = geo_matrix_translate(geo_vector(42.0f, -1337.0f, .1f));
    const GeoMatrix mR    = geo_matrix_rotate_x(math_pi_f32 * .25f);
    const GeoMatrix mS    = geo_matrix_scale(scale);
    const GeoMatrix m1    = geo_matrix_mul(&mT, &mR);
    const GeoMatrix m2    = geo_matrix_mul(&m1, &mS);

    check_eq_vector(geo_matrix_to_scale(&m2), scale);
  }

  it("can be decomposed and recomposed") {
    const GeoVector orgT = geo_vector(42.0f, -1337.0f, .1f);
    const GeoQuat   orgR = geo_quat_angle_axis(geo_right, math_pi_f32 * .25f);

    const GeoMatrix mT   = geo_matrix_translate(orgT);
    const GeoMatrix mR   = geo_matrix_from_quat(orgR);
    const GeoMatrix mOrg = geo_matrix_mul(&mT, &mR);

    const GeoVector extTranslation = geo_matrix_to_translation(&mOrg);
    check_eq_vector(extTranslation, orgT);

    const GeoQuat extRotation = geo_matrix_to_quat(&mOrg);
    check_eq_quat(extRotation, orgR);

    check_eq_matrix(geo_matrix_trs(extTranslation, extRotation, geo_vector(1, 1, 1)), mOrg);
  }

  it("returns a vector 45 degrees rotated when transforming by a rotate by 45 matrix") {
    const f32       angle = math_pi_f32 * .25f;
    const GeoMatrix mX    = geo_matrix_rotate_x(angle);
    const GeoMatrix mY    = geo_matrix_rotate_y(angle);
    const GeoMatrix mZ    = geo_matrix_rotate_z(angle);
    const GeoVector v1    = geo_vector_norm(geo_vector(0, -2, 3));
    const GeoVector v2    = geo_vector_norm(geo_vector(-2, 0, 3));
    const GeoVector v3    = geo_vector_norm(geo_vector(-2, 3, 0));

    check_eq_float(geo_vector_angle(geo_matrix_transform(&mX, v1), v1), angle, 1e-6);
    check_eq_float(geo_vector_angle(geo_matrix_transform(&mY, v2), v2), angle, 1e-6);
    check_eq_float(geo_vector_angle(geo_matrix_transform(&mZ, v3), v3), angle, 1e-6);
  }

  it("flips the axis when transforming a vector with a 180 degrees rotation matrix") {
    const f32       angle = math_pi_f32;
    const GeoMatrix mX    = geo_matrix_rotate_x(angle);
    const GeoMatrix mY    = geo_matrix_rotate_y(angle);
    const GeoMatrix mZ    = geo_matrix_rotate_z(angle);

    check_eq_vector(geo_matrix_transform(&mX, geo_vector(0, 1, 0)), geo_vector(0, -1, 0));
    check_eq_vector(geo_matrix_transform(&mY, geo_vector(0, 0, 1)), geo_vector(0, 0, -1));
    check_eq_vector(geo_matrix_transform(&mZ, geo_vector(1, 0, 0)), geo_vector(-1, 0, 0));
  }

  it("returns the same rotation then a quaternion when rotating over a dimensional axis") {
    const f32       angle = 42 * math_deg_to_rad;
    const GeoMatrix mX    = geo_matrix_rotate_x(angle);
    const GeoMatrix mY    = geo_matrix_rotate_y(angle);
    const GeoMatrix mZ    = geo_matrix_rotate_z(angle);

    const GeoQuat qX = geo_quat_angle_axis(geo_right, angle);
    const GeoQuat qY = geo_quat_angle_axis(geo_up, angle);
    const GeoQuat qZ = geo_quat_angle_axis(geo_forward, angle);

    const GeoVector v = geo_vector(.42f, 13.37f, -42);

    check_eq_vector(geo_matrix_transform(&mX, v), geo_quat_rotate(qX, v));
    check_eq_vector(geo_matrix_transform(&mY, v), geo_quat_rotate(qY, v));
    check_eq_vector(geo_matrix_transform(&mZ, v), geo_quat_rotate(qZ, v));
  }

  it("can convert a quaternion to a rotation matrix") {
    {
      const f32       angle = 42 * math_deg_to_rad;
      const GeoMatrix mX    = geo_matrix_rotate_x(angle);
      const GeoMatrix mY    = geo_matrix_rotate_y(angle);
      const GeoMatrix mZ    = geo_matrix_rotate_z(angle);

      const GeoQuat qX = geo_quat_angle_axis(geo_right, angle);
      const GeoQuat qY = geo_quat_angle_axis(geo_up, angle);
      const GeoQuat qZ = geo_quat_angle_axis(geo_forward, angle);

      check_eq_matrix(geo_matrix_from_quat(qX), mX);
      check_eq_matrix(geo_matrix_from_quat(qY), mY);
      check_eq_matrix(geo_matrix_from_quat(qZ), mZ);
    }
    {
      const GeoQuat q =
          geo_quat_mul(geo_quat_angle_axis(geo_up, 42), geo_quat_angle_axis(geo_right, 13));
      const GeoVector newX        = geo_quat_rotate(q, geo_right);
      const GeoVector newY        = geo_quat_rotate(q, geo_up);
      const GeoVector newZ        = geo_quat_rotate(q, geo_forward);
      const GeoMatrix matFromAxes = geo_matrix_rotate(newX, newY, newZ);
      check_eq_matrix(matFromAxes, geo_matrix_from_quat(q));
    }
  }

  it("can be converted to a quaternion") {
    {
      const GeoMatrix m = {.columns = {{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}}};
      check_eq_quat(geo_matrix_to_quat(&m), geo_quat_angle_axis(geo_up, math_pi_f32));
    }
    {
      const GeoQuat q1 =
          geo_quat_mul(geo_quat_angle_axis(geo_up, 42), geo_quat_angle_axis(geo_right, 13));
      const GeoMatrix m  = geo_matrix_from_quat(q1);
      const GeoQuat   q2 = geo_matrix_to_quat(&m);
      const GeoVector v  = {.42f, 13.37f, -42};
      check_eq_vector(geo_quat_rotate(q1, v), geo_quat_rotate(q2, v));
    }
  }

  it("scales vectors to clip-space when transforming by an orthogonal projection matrix") {
    const GeoMatrix m = geo_matrix_proj_ortho(10, 5, -2, 2);
    check_eq_vector(geo_matrix_transform(&m, geo_vector(0, 0, 0, 1)), geo_vector(0, 0, .5f, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(+5, 0, 0, 1)), geo_vector(+1, 0, .5f, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(-5, 0, 0, 1)), geo_vector(-1, 0, .5f, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(-5, 5, 0, 1)), geo_vector(-1, -2, .5f, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(-5, -5, 0, 1)), geo_vector(-1, 2, .5f, 1));

    // Reversed-z so near is at depth 1 and far is at depth 0.
    check_eq_vector(geo_matrix_transform(&m, geo_vector(-5, 0, -2, 1)), geo_vector(-1, 0, 1, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(-5, 0, +2, 1)), geo_vector(-1, 0, 0, 1));
  }

  it("scales vectors to clip-space when transforming by an perspective projection matrix") {
    const f32       fov = 90 * math_deg_to_rad;
    const GeoMatrix m   = geo_matrix_proj_pers(fov, fov, 0.42f);

    // Reversed-z depth so, near plane is at depth 1.
    const GeoVector v1 = geo_matrix_transform(&m, geo_vector(0, 0, 0.42f, 1));
    check_eq_vector(geo_vector_perspective_div(v1), geo_vector(0, 0, 1));

    // Reversed-z depth with infinite far plane, so infinite z is at depth 0.
    const GeoVector v2 = geo_matrix_transform(&m, geo_vector(0, 0, 999999, 1));
    check_eq_vector(geo_vector_perspective_div(v2), geo_vector(0, 0, 0));
  }

  it("can extract 4 frustum planes from a orthographic projection matrix") {
    const GeoMatrix m = geo_matrix_proj_ortho(10, 5, -2, 2);
    GeoPlane        frustum[4]; // Left, Right, Top, Bottom.
    geo_matrix_frustum4(&m, frustum);

    check_eq_vector(frustum[0].normal, geo_right);
    check_eq_float(frustum[0].distance, 5.0f, 1e-6);

    check_eq_vector(frustum[1].normal, geo_left);
    check_eq_float(frustum[1].distance, 5.0f, 1e-6);

    check_eq_vector(frustum[2].normal, geo_up);
    check_eq_float(frustum[2].distance, 2.5f, 1e-6);

    check_eq_vector(frustum[3].normal, geo_down);
    check_eq_float(frustum[3].distance, 2.5f, 1e-6);
  }
}
