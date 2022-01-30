#include "core_diag.h"
#include "core_math.h"
#include "geo_matrix.h"

static void assert_normalized(const GeoVector v) {
  MAYBE_UNUSED const f32 sqrMag = geo_vector_mag_sqr(v);
  diag_assert_msg(math_abs(sqrMag - 1) < 1e-4, "Given vector is not normalized");
}

static void assert_orthogonal(const GeoVector a, const GeoVector b) {
  MAYBE_UNUSED const f32 dot = geo_vector_dot(a, b);
  diag_assert_msg(math_abs(dot) < 1e-4, "Given vectors are not orthogonal to eachother");
}

static void assert_orthonormal(const GeoVector right, const GeoVector up, const GeoVector fwd) {
  assert_normalized(right);
  assert_normalized(up);
  assert_normalized(fwd);
  assert_orthogonal(right, up);
  assert_orthogonal(up, fwd);
}

GeoMatrix geo_matrix_ident() {
  /**
   * [ 1,  0,  0,  0 ]
   * [ 0,  1,  0,  0 ]
   * [ 0,  0,  1,  0 ]
   * [ 0,  0,  0,  1 ]
   */
  return (GeoMatrix){
      .columns = {
          {1, 0, 0, 0},
          {0, 1, 0, 0},
          {0, 0, 1, 0},
          {0, 0, 0, 1},
      }};
}

GeoVector geo_matrix_row(const GeoMatrix* m, const usize index) {
  return (GeoVector){
      .x = m->columns[0].comps[index],
      .y = m->columns[1].comps[index],
      .z = m->columns[2].comps[index],
      .w = m->columns[3].comps[index],
  };
}

GeoMatrix geo_matrix_mul(const GeoMatrix* a, const GeoMatrix* b) {
  GeoMatrix res;
  for (usize col = 0; col != 4; ++col) {
    for (usize row = 0; row != 4; ++row) {
      res.columns[col].comps[row] = geo_vector_dot(geo_matrix_row(a, row), b->columns[col]);
    }
  }
  return res;
}

GeoVector geo_matrix_transform(const GeoMatrix* m, const GeoVector vec) {
  return (GeoVector){
      .x = geo_vector_dot(geo_matrix_row(m, 0), vec),
      .y = geo_vector_dot(geo_matrix_row(m, 1), vec),
      .z = geo_vector_dot(geo_matrix_row(m, 2), vec),
      .w = geo_vector_dot(geo_matrix_row(m, 3), vec),
  };
}

GeoMatrix geo_matrix_transpose(const GeoMatrix* m) {
  return (GeoMatrix){
      .columns = {
          geo_matrix_row(m, 0),
          geo_matrix_row(m, 1),
          geo_matrix_row(m, 2),
          geo_matrix_row(m, 3),
      }};
}

GeoMatrix geo_matrix_translate(const GeoVector translation) {
  /**
   * [ 1,  0,  0,  x ]
   * [ 0,  1,  0,  y ]
   * [ 0,  0,  1,  z ]
   * [ 0,  0,  0,  1 ]
   */
  return (GeoMatrix){
      .columns = {
          {1, 0, 0, 0},
          {0, 1, 0, 0},
          {0, 0, 1, 0},
          {translation.x, translation.y, translation.z, 1},
      }};
}

GeoMatrix geo_matrix_scale(const GeoVector scale) {
  /**
   * [ sx, 0,  0,  0 ]
   * [ 0,  sy, 0,  0 ]
   * [ 0,  0,  sz, 0 ]
   * [ 0,  0,  0,  1 ]
   */
  return (GeoMatrix){
      .columns = {
          {scale.x, 0, 0, 0},
          {0, scale.y, 0, 0},
          {0, 0, scale.z, 0},
          {0, 0, 0, 1},
      }};
}

GeoMatrix geo_matrix_rotate_x(const f32 angle) {
  /**
   * [ 1,  0,   0,    0 ]
   * [ 0,  cos, -sin, 0 ]
   * [ 0,  sin, cos,  0 ]
   * [ 0,  0,   0,    1 ]
   */
  const f32 c = math_cos_f32(angle);
  const f32 s = math_sin_f32(angle);
  return (GeoMatrix){
      .columns = {
          {1, 0, 0, 0},
          {0, c, s, 0},
          {0, -s, c, 0},
          {0, 0, 0, 1},
      }};
}

GeoMatrix geo_matrix_rotate_y(const f32 angle) {
  /**
   * [ cos,  0,  sin, 0 ]
   * [ 0,    1,  0,   0 ]
   * [ -sin, 0,  cos, 0 ]
   * [ 0,    0,  0,   1 ]
   */
  const f32 c = math_cos_f32(angle);
  const f32 s = math_sin_f32(angle);
  return (GeoMatrix){
      .columns = {
          {c, 0, -s, 0},
          {0, 1, 0, 0},
          {s, 0, c, 0},
          {0, 0, 0, 1},
      }};
}

GeoMatrix geo_matrix_rotate_z(const f32 angle) {
  /*
   * [ cos, -sin, 0,  0 ]
   * [ sin, cos,  0,  0 ]
   * [ 0,   0,    1,  0 ]
   * [ 0,   0,    0,  1 ]
   */
  const f32 c = math_cos_f32(angle);
  const f32 s = math_sin_f32(angle);
  return (GeoMatrix){
      .columns = {
          {c, s, 0, 0},
          {-s, c, 0, 0},
          {0, 0, 1, 0},
          {0, 0, 0, 1},
      }};
}

GeoMatrix geo_matrix_rotate(const GeoVector right, const GeoVector up, const GeoVector fwd) {
  /**
   * NOTE: An alternative api could be that we allow a non-orthonormal set of axis as input and just
   * normalize and reconstruct the axes. This would however be wastefull when you already have
   * orthonormal axes.
   */
  assert_orthonormal(right, up, fwd);

  /**
   * [ right.x,   up.x,   fwd.x,  0 ]
   * [ right.y,   up.y,   fwd.y,  0 ]
   * [ right.z,   up.z,   fwd.z,  0 ]
   * [       0,      0,       0,  1 ]
   */
  return (GeoMatrix){
      .columns = {
          {right.x, right.y, right.z, 0},
          {up.x, up.y, up.z, 0},
          {fwd.x, fwd.y, fwd.z, 0},
          {0, 0, 0, 1},
      }};
}

GeoMatrix geo_matrix_from_quat(const GeoQuat quat) {
  /**
   * [ 1 - 2y² - 2z²,   2xy + 2wz ,     2xz - 2wy,     0 ]
   * [ 2xy - 2wz,       1 - 2x² - 2z²,  2yz + 2wx,     0 ]
   * [ 2xz + 2wy,       2yz - 2wx,      1 - 2x² - 2y², 0 ]
   * [ 0,               0,              0,             1 ]
   */
  const f32 x = quat.x;
  const f32 y = quat.y;
  const f32 z = quat.z;
  const f32 w = quat.w;

  return (GeoMatrix){
      .columns = {
          {1 - 2 * y * y - 2 * z * z, 2 * x * y + 2 * w * z, 2 * x * z - 2 * w * y, 0},
          {2 * x * y - 2 * w * z, 1 - 2 * x * x - 2 * z * z, 2 * y * z + 2 * w * x, 0},
          {2 * x * z + 2 * w * y, 2 * y * z - 2 * w * x, 1 - 2 * x * x - 2 * y * y},
          {0, 0, 0, 1},
      }};
}

GeoQuat geo_matrix_to_quat(const GeoMatrix* m) {
  /**
   * qw = √(1 + m00 + m11 + m22) / 2
   * qx = (m21 - m12) / (4 * qw)
   * qy = (m02 - m20) / (4 * qw)
   * qz = (m10 - m01) / (4 * qw)
   *
   * Implementation based on:
   * https://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
   */

  const f32 trace = m->columns[0].x + m->columns[1].y + m->columns[2].z; // Sum of diag elements.
  if (trace > f32_epsilon) {
    // Trace is positive
    const f32 s = math_sqrt_f32(trace + 1) * 2;
    return (GeoQuat){
        .x = (m->columns[1].z - m->columns[2].y) / s,
        .y = (m->columns[2].x - m->columns[0].z) / s,
        .z = (m->columns[0].y - m->columns[1].x) / s,
        .w = s * .25f,
    };
  }

  // Trace zero or negative.
  // Find the biggest diagonal element.
  if (m->columns[0].x > m->columns[1].y && m->columns[0].x > m->columns[2].z) {
    // [0, 0] is the biggest.
    const f32 s = math_sqrt_f32(1 + m->columns[0].x - m->columns[1].y - m->columns[2].z) * 2;
    return (GeoQuat){
        .x = s * .25f,
        .y = (m->columns[1].x + m->columns[0].y) / s,
        .z = (m->columns[2].x + m->columns[0].z) / s,
        .w = (m->columns[1].z - m->columns[2].y) / s,
    };
  }

  if (m->columns[1].y > m->columns[2].z) {
    // [1, 1] is the biggest.
    const f32 s = math_sqrt_f32(1 + m->columns[1].y - m->columns[0].x - m->columns[2].z) * 2;
    return (GeoQuat){
        .x = (m->columns[1].x + m->columns[0].y) / s,
        .y = s * .25f,
        .z = (m->columns[2].y + m->columns[1].z) / s,
        .w = (m->columns[2].x - m->columns[0].z) / s,
    };
  }

  // [2, 2] is the biggest.
  const f32 s = math_sqrt_f32(1 + m->columns[2].z - m->columns[0].x - m->columns[1].y) * 2;
  return (GeoQuat){
      .x = (m->columns[2].x + m->columns[0].z) / s,
      .y = (m->columns[2].y + m->columns[1].z) / s,
      .z = s * .25f,
      .w = (m->columns[0].y - m->columns[1].x) / s,
  };
}

GeoMatrix
geo_matrix_proj_ortho(const f32 width, const f32 height, const f32 zNear, const f32 zFar) {
  /**
   * [ 2 / w,       0,           0,           0            ]
   * [ 0,           -(2 / h),    0,           0            ]
   * [ 0,           0,           1 / (n - f), -f / (n - f) ]
   * [ 0,           0,           0,           1            ]
   *
   * NOTE: Setup for reversed-z depth so near objects are at depth 1 and far at 0.
   */
  return (GeoMatrix){
      .columns = {
          {2 / width, 0, 0, 0},
          {0, -(2 / height), 0, 0},
          {0, 0, 1 / (zNear - zFar), 0},
          {0, 0, -zFar / (zNear - zFar), 1},
      }};
}

GeoMatrix
geo_matrix_proj_ortho_ver(const f32 size, const f32 aspect, const f32 zNear, const f32 zFar) {
  return geo_matrix_proj_ortho(size, size / aspect, zNear, zFar);
}

GeoMatrix
geo_matrix_proj_ortho_hor(const f32 size, const f32 aspect, const f32 zNear, const f32 zFar) {
  return geo_matrix_proj_ortho(size * aspect, size, zNear, zFar);
}

GeoMatrix geo_matrix_proj_pers(const f32 horAngle, const f32 verAngle, const f32 zNear) {
  /**
   * [ 1 / tan(hor / 2),  0,                    0,               0      ]
   * [ 0,                 -(1 / tan(ver / 2)),  0,               0      ]
   * [ 0,                 0,                    0,               zNear  ]
   * [ 0,                 0,                    1,               0      ]
   *
   * NOTE: Setup for reversed-z with an infinite far plane, so near objects are at depth 1 and depth
   * approaches 0 at infinite z.
   */
  return (GeoMatrix){
      .columns = {
          {1 / math_tan_f32(horAngle * .5f), 0, 0, 0},
          {0, -(1 / math_tan_f32(verAngle * .5f)), 0, 0},
          {0, 0, 0, 1},
          {0, 0, zNear, 0},
      }};
}

GeoMatrix geo_matrix_proj_pers_ver(const f32 verAngle, const f32 aspect, const f32 zNear) {
  const f32 horAngle = math_atan_f32(math_tan_f32(verAngle * .5f) * aspect) * 2.f;
  return geo_matrix_proj_pers(horAngle, verAngle, zNear);
}

GeoMatrix geo_matrix_proj_pers_hor(const f32 horAngle, const f32 aspect, const f32 zNear) {
  const f32 verAngle = math_atan_f32(math_tan_f32(horAngle * .5f) / aspect) * 2.f;
  return geo_matrix_proj_pers(horAngle, verAngle, zNear);
}

void geo_matrix_frustum4(const GeoMatrix* proj, GeoPlane out[4]) {
  /**
   * Extract the frustum planes from the proj matrix.
   * More information: http://www.cs.otago.ac.nz/postgrads/alexis/planeExtraction.pdf
   */

  // Left clipping plane.
  out[0] = (GeoPlane){
      .normal.x = proj->columns[0].w + proj->columns[0].x,
      .normal.y = proj->columns[1].w + proj->columns[1].x,
      .normal.z = proj->columns[2].w + proj->columns[2].x,
      .distance = proj->columns[3].w + proj->columns[3].x,
  };

  // Right clipping plane.
  out[1] = (GeoPlane){
      .normal.x = proj->columns[0].w - proj->columns[0].x,
      .normal.y = proj->columns[1].w - proj->columns[1].x,
      .normal.z = proj->columns[2].w - proj->columns[2].x,
      .distance = proj->columns[3].w - proj->columns[3].x,
  };

  // Top clipping plane.
  out[2] = (GeoPlane){
      .normal.x = proj->columns[0].w - proj->columns[0].y,
      .normal.y = proj->columns[1].w - proj->columns[1].y,
      .normal.z = proj->columns[2].w - proj->columns[2].y,
      .distance = proj->columns[3].w - proj->columns[3].y,
  };

  // Bottom clipping plane.
  out[3] = (GeoPlane){
      .normal.x = proj->columns[0].w + proj->columns[0].y,
      .normal.y = proj->columns[1].w + proj->columns[1].y,
      .normal.z = proj->columns[2].w + proj->columns[2].y,
      .distance = proj->columns[3].w + proj->columns[3].y,
  };

  // Normalize the planes.
  for (usize i = 0; i != 4; ++i) {
    const f32 mag = geo_vector_mag(out[i].normal);
    out[i].normal = geo_vector_div(out[i].normal, mag);
    out[i].distance /= mag;
  }
}
