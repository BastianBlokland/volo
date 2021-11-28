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
      .columns[0].x = 1,
      .columns[1].y = 1,
      .columns[2].z = 1,
      .columns[3].w = 1,
  };
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
      .columns[0] = geo_matrix_row(m, 0),
      .columns[1] = geo_matrix_row(m, 1),
      .columns[2] = geo_matrix_row(m, 2),
      .columns[3] = geo_matrix_row(m, 3),
  };
}

GeoMatrix geo_matrix_translate(const GeoVector translation) {
  /**
   * [ 1,  0,  0,  x ]
   * [ 0,  1,  0,  y ]
   * [ 0,  0,  1,  z ]
   * [ 0,  0,  0,  1 ]
   */
  return (GeoMatrix){
      .columns[0].x = 1,
      .columns[1].y = 1,
      .columns[2].z = 1,
      .columns[3]   = {translation.x, translation.y, translation.z, 1},
  };
}

GeoMatrix geo_matrix_scale(const GeoVector scale) {
  /**
   * [ sx, 0,  0,  0 ]
   * [ 0,  sy, 0,  0 ]
   * [ 0,  0,  sz, 0 ]
   * [ 0,  0,  0,  1 ]
   */
  return (GeoMatrix){
      .columns[0].x = scale.x,
      .columns[1].y = scale.y,
      .columns[2].z = scale.z,
      .columns[3].w = 1,
  };
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
      .columns[0].comps[0] = 1,
      .columns[1].comps[1] = s,
      .columns[1].comps[2] = s,
      .columns[2].comps[1] = -s,
      .columns[2].comps[2] = c,
      .columns[3].comps[3] = 1,
  };
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
      .columns[0].comps[0] = c,
      .columns[0].comps[2] = -s,
      .columns[1].comps[1] = 1,
      .columns[2].comps[0] = s,
      .columns[2].comps[2] = c,
      .columns[3].comps[3] = 1,
  };
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
      .columns[0].comps[0] = c,
      .columns[0].comps[1] = s,
      .columns[1].comps[0] = -s,
      .columns[1].comps[1] = c,
      .columns[2].comps[2] = 1,
      .columns[3].comps[3] = 1,
  };
}

GeoMatrix geo_matrix_rotate(GeoVector right, GeoVector up, GeoVector fwd) {
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
      .columns[0]          = {right.x, right.y, right.z},
      .columns[1]          = {up.x, up.y, up.z},
      .columns[2]          = {fwd.x, fwd.y, fwd.z},
      .columns[3].comps[3] = 1,
  };
}

GeoMatrix geo_matrix_proj_ortho(f32 width, f32 height, f32 zNear, f32 zFar) {
  /**
   * [ 2 / w,       0,           0,           0            ]
   * [ 0,           -(2 / h),    0,           0            ]
   * [ 0,           0,           1 / (n - f), -f / (n - f) ]
   * [ 0,           0,           0,           1            ]
   *
   * NOTE: Setup for reversed-z depth so near objects are at depth 1 and far at 0.
   */
  return (GeoMatrix){
      .columns[0].comps[0] = 2 / width,
      .columns[1].comps[1] = -(2 / height),
      .columns[2].comps[2] = 1 / (zNear - zFar),
      .columns[3].comps[2] = -zFar / (zNear - zFar),
      .columns[3].comps[3] = 1,
  };
}

GeoMatrix geo_matrix_proj_pers(f32 horAngle, f32 verAngle, f32 zNear) {
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
      .columns[0].comps[0] = 1 / math_tan_f32(horAngle * .5f),
      .columns[1].comps[1] = -(1 / math_tan_f32(verAngle * .5f)),
      .columns[2].comps[2] = 0,
      .columns[3].comps[2] = zNear,
      .columns[2].comps[3] = 1,
  };
}

GeoMatrix geo_matrix_proj_pers_ver(const f32 verAngle, const f32 aspect, const f32 zNear) {
  const f32 horAngle = math_atan_f32(math_tan_f32(verAngle * .5f) * aspect) * 2.f;
  return geo_matrix_proj_pers(horAngle, verAngle, zNear);
}

GeoMatrix geo_matrix_proj_pers_hor(f32 horAngle, f32 aspect, f32 zNear) {
  const f32 verAngle = math_atan_f32(math_tan_f32(horAngle * .5f) / aspect) * 2.f;
  return geo_matrix_proj_pers(horAngle, verAngle, zNear);
}
