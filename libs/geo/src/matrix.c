#include "core_diag.h"
#include "core_math.h"
#include "geo_matrix.h"

#include "intrinsic_internal.h"

#define geo_matrix_simd_enable 1

#if geo_matrix_simd_enable
#include "simd_sse_internal.h"
#endif

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
#if geo_matrix_simd_enable
  GeoMatrix     res;
  const SimdVec col0 = simd_vec_load(a->columns[0].comps);
  const SimdVec col1 = simd_vec_load(a->columns[1].comps);
  const SimdVec col2 = simd_vec_load(a->columns[2].comps);
  const SimdVec col3 = simd_vec_load(a->columns[3].comps);
  for (usize i = 0; i != 4; ++i) {
    const SimdVec rowX   = simd_vec_broadcast(b->columns[i].x);
    const SimdVec rowY   = simd_vec_broadcast(b->columns[i].y);
    const SimdVec rowZ   = simd_vec_broadcast(b->columns[i].z);
    const SimdVec rowW   = simd_vec_broadcast(b->columns[i].w);
    const SimdVec resCol = simd_vec_add(
        simd_vec_add(simd_vec_mul(rowX, col0), simd_vec_mul(rowY, col1)),
        simd_vec_add(simd_vec_mul(rowZ, col2), simd_vec_mul(rowW, col3)));
    simd_vec_store(resCol, res.columns[i].comps);
  }
  return res;
#else
  GeoMatrix res;
  for (usize col = 0; col != 4; ++col) {
    for (usize row = 0; row != 4; ++row) {
      res.columns[col].comps[row] = geo_vector_dot(geo_matrix_row(a, row), b->columns[col]);
    }
  }
  return res;
#endif
}

void geo_matrix_mul_batch(
    const GeoMatrix* a, const GeoMatrix* b, GeoMatrix* restrict out, const u32 cnt) {
  const GeoMatrix* aEnd = a + cnt;
  for (; a != aEnd; ++a, ++b, ++out) {
    *out = geo_matrix_mul(a, b);
  }
}

GeoVector geo_matrix_transform(const GeoMatrix* m, const GeoVector vec) {
  return (GeoVector){
      .x = geo_vector_dot(geo_matrix_row(m, 0), vec),
      .y = geo_vector_dot(geo_matrix_row(m, 1), vec),
      .z = geo_vector_dot(geo_matrix_row(m, 2), vec),
      .w = geo_vector_dot(geo_matrix_row(m, 3), vec),
  };
}

GeoVector geo_matrix_transform3(const GeoMatrix* m, const GeoVector vec) {
  return (GeoVector){
      .x = geo_vector_dot(geo_matrix_row(m, 0), vec),
      .y = geo_vector_dot(geo_matrix_row(m, 1), vec),
      .z = geo_vector_dot(geo_matrix_row(m, 2), vec),
  };
}

GeoVector geo_matrix_transform3_point(const GeoMatrix* m, const GeoVector vec) {
  return (GeoVector){
      .x = geo_vector_dot(geo_matrix_row(m, 0), vec) + m->columns[3].x,
      .y = geo_vector_dot(geo_matrix_row(m, 1), vec) + m->columns[3].y,
      .z = geo_vector_dot(geo_matrix_row(m, 2), vec) + m->columns[3].z,
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

GeoMatrix geo_matrix_inverse(const GeoMatrix* m) {
  /**
   * 4x4 Matrix inverse routine generated using 'N-Matrix-Programmer' by: 'willnode'.
   * Repository: https://github.com/willnode/N-Matrix-Programmer
   */
  const f32 a2323 = m->columns[2].z * m->columns[3].w - m->columns[3].z * m->columns[2].w;
  const f32 a1323 = m->columns[1].z * m->columns[3].w - m->columns[3].z * m->columns[1].w;
  const f32 a1223 = m->columns[1].z * m->columns[2].w - m->columns[2].z * m->columns[1].w;
  const f32 a0323 = m->columns[0].z * m->columns[3].w - m->columns[3].z * m->columns[0].w;
  const f32 a0223 = m->columns[0].z * m->columns[2].w - m->columns[2].z * m->columns[0].w;
  const f32 a0123 = m->columns[0].z * m->columns[1].w - m->columns[1].z * m->columns[0].w;
  const f32 a2313 = m->columns[2].y * m->columns[3].w - m->columns[3].y * m->columns[2].w;
  const f32 a1313 = m->columns[1].y * m->columns[3].w - m->columns[3].y * m->columns[1].w;
  const f32 a1213 = m->columns[1].y * m->columns[2].w - m->columns[2].y * m->columns[1].w;
  const f32 a2312 = m->columns[2].y * m->columns[3].z - m->columns[3].y * m->columns[2].z;
  const f32 a1312 = m->columns[1].y * m->columns[3].z - m->columns[3].y * m->columns[1].z;
  const f32 a1212 = m->columns[1].y * m->columns[2].z - m->columns[2].y * m->columns[1].z;
  const f32 a0313 = m->columns[0].y * m->columns[3].w - m->columns[3].y * m->columns[0].w;
  const f32 a0213 = m->columns[0].y * m->columns[2].w - m->columns[2].y * m->columns[0].w;
  const f32 a0312 = m->columns[0].y * m->columns[3].z - m->columns[3].y * m->columns[0].z;
  const f32 a0212 = m->columns[0].y * m->columns[2].z - m->columns[2].y * m->columns[0].z;
  const f32 a0113 = m->columns[0].y * m->columns[1].w - m->columns[1].y * m->columns[0].w;
  const f32 a0112 = m->columns[0].y * m->columns[1].z - m->columns[1].y * m->columns[0].z;

  f32 det =
      (m->columns[0].x *
           (m->columns[1].y * a2323 - m->columns[2].y * a1323 + m->columns[3].y * a1223) -
       m->columns[1].x *
           (m->columns[0].y * a2323 - m->columns[2].y * a0323 + m->columns[3].y * a0223) +
       m->columns[2].x *
           (m->columns[0].y * a1323 - m->columns[1].y * a0323 + m->columns[3].y * a0123) -
       m->columns[3].x *
           (m->columns[0].y * a1223 - m->columns[1].y * a0223 + m->columns[2].y * a0123));

  diag_assert_msg(det != 0.0f, "Non invertible matrix");
  det = 1.0f / det;

  return (GeoMatrix){
      .columns[0] =
          {
              det * (m->columns[1].y * a2323 - m->columns[2].y * a1323 + m->columns[3].y * a1223),
              det * -(m->columns[0].y * a2323 - m->columns[2].y * a0323 + m->columns[3].y * a0223),
              det * (m->columns[0].y * a1323 - m->columns[1].y * a0323 + m->columns[3].y * a0123),
              det * -(m->columns[0].y * a1223 - m->columns[1].y * a0223 + m->columns[2].y * a0123),
          },
      .columns[1] =
          {
              det * -(m->columns[1].x * a2323 - m->columns[2].x * a1323 + m->columns[3].x * a1223),
              det * (m->columns[0].x * a2323 - m->columns[2].x * a0323 + m->columns[3].x * a0223),
              det * -(m->columns[0].x * a1323 - m->columns[1].x * a0323 + m->columns[3].x * a0123),
              det * (m->columns[0].x * a1223 - m->columns[1].x * a0223 + m->columns[2].x * a0123),
          },
      .columns[2] =
          {
              det * (m->columns[1].x * a2313 - m->columns[2].x * a1313 + m->columns[3].x * a1213),
              det * -(m->columns[0].x * a2313 - m->columns[2].x * a0313 + m->columns[3].x * a0213),
              det * (m->columns[0].x * a1313 - m->columns[1].x * a0313 + m->columns[3].x * a0113),
              det * -(m->columns[0].x * a1213 - m->columns[1].x * a0213 + m->columns[2].x * a0113),
          },
      .columns[3] =
          {
              det * -(m->columns[1].x * a2312 - m->columns[2].x * a1312 + m->columns[3].x * a1212),
              det * (m->columns[0].x * a2312 - m->columns[2].x * a0312 + m->columns[3].x * a0212),
              det * -(m->columns[0].x * a1312 - m->columns[1].x * a0312 + m->columns[3].x * a0112),
              det * (m->columns[0].x * a1212 - m->columns[1].x * a0212 + m->columns[2].x * a0112),
          },
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
      .columns = {
          {1, 0, 0, 0},
          {0, 1, 0, 0},
          {0, 0, 1, 0},
          {translation.x, translation.y, translation.z, 1},
      }};
}

GeoVector geo_matrix_to_translation(const GeoMatrix* m) {
  return geo_vector(m->columns[3].x, m->columns[3].y, m->columns[3].z, 0);
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

GeoVector geo_matrix_to_scale(const GeoMatrix* m) {
  const GeoVector xAxis = geo_matrix_transform3(m, geo_right);
  const GeoVector yAxis = geo_matrix_transform3(m, geo_up);
  const GeoVector zAxis = geo_matrix_transform3(m, geo_forward);

  return geo_vector(geo_vector_mag(xAxis), geo_vector_mag(yAxis), geo_vector_mag(zAxis));
}

GeoMatrix geo_matrix_rotate_x(const f32 angle) {
  /**
   * [ 1,  0,   0,    0 ]
   * [ 0,  cos, -sin, 0 ]
   * [ 0,  sin, cos,  0 ]
   * [ 0,  0,   0,    1 ]
   */
  const f32 c = intrinsic_cos_f32(angle);
  const f32 s = intrinsic_sin_f32(angle);
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
  const f32 c = intrinsic_cos_f32(angle);
  const f32 s = intrinsic_sin_f32(angle);
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
  const f32 c = intrinsic_cos_f32(angle);
  const f32 s = intrinsic_sin_f32(angle);
  return (GeoMatrix){
      .columns = {
          {c, s, 0, 0},
          {-s, c, 0, 0},
          {0, 0, 1, 0},
          {0, 0, 0, 1},
      }};
}

GeoMatrix geo_matrix_rotate(const GeoVector right, const GeoVector up, const GeoVector fwd) {
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

GeoMatrix geo_matrix_rotate_look(const GeoVector forward, const GeoVector upRef) {
#if geo_matrix_simd_enable
  const SimdVec vForward       = simd_vec_load(forward.comps);
  const SimdVec vForwardSqrMag = simd_vec_dot3(vForward, vForward);
  if (UNLIKELY(simd_vec_x(vForwardSqrMag) <= f32_epsilon)) {
    return geo_matrix_ident();
  }

  const SimdVec vUpRef       = simd_vec_load(upRef.comps);
  const SimdVec vUpRefSqrMag = simd_vec_dot3(vUpRef, vUpRef);
  if (UNLIKELY(simd_vec_x(vUpRefSqrMag) <= f32_epsilon)) {
    return geo_matrix_ident();
  }

  const SimdVec vForwardMag  = simd_vec_sqrt(vForwardSqrMag);
  const SimdVec vForwardNorm = simd_vec_div(vForward, vForwardMag);
  const SimdVec vRight       = simd_vec_cross3(vUpRef, vForwardNorm);
  const SimdVec vRightSqrMag = simd_vec_dot3(vRight, vRight);
  const SimdVec vRightNorm   = LIKELY(simd_vec_x(vRightSqrMag) > f32_epsilon)
                                   ? simd_vec_div(vRight, simd_vec_sqrt(vRightSqrMag))
                                   : simd_vec_set(1, 0, 0, 0);
  const SimdVec vUpNorm      = simd_vec_cross3(vForwardNorm, vRightNorm);

  GeoMatrix res;
  simd_vec_store(vRightNorm, res.columns[0].comps);
  simd_vec_store(vUpNorm, res.columns[1].comps);
  simd_vec_store(vForwardNorm, res.columns[2].comps);
  simd_vec_store(simd_vec_set(0, 0, 0, 1), res.columns[3].comps);
  return res;
#else
  if (UNLIKELY(geo_vector_mag_sqr(forward) <= f32_epsilon)) {
    return geo_matrix_ident();
  }
  if (UNLIKELY(geo_vector_mag_sqr(upRef) <= f32_epsilon)) {
    return geo_matrix_ident();
  }
  const GeoVector fwdNorm     = geo_vector_norm(forward);
  GeoVector       right       = geo_vector_cross3(upRef, fwdNorm);
  const f32       rightMagSqr = geo_vector_mag_sqr(right);
  if (LIKELY(rightMagSqr > f32_epsilon)) {
    right = geo_vector_div(right, intrinsic_sqrt_f32(rightMagSqr));
  } else {
    right = geo_right;
  }
  const GeoVector upNorm = geo_vector_cross3(fwdNorm, right);
  return geo_matrix_rotate(right, upNorm, fwdNorm);
#endif
}

GeoMatrix geo_matrix_from_quat(const GeoQuat quat) {
/**
 * [ 1 - 2y² - 2z²,   2xy + 2wz ,     2xz - 2wy,     0 ]
 * [ 2xy - 2wz,       1 - 2x² - 2z²,  2yz + 2wx,     0 ]
 * [ 2xz + 2wy,       2yz - 2wx,      1 - 2x² - 2y², 0 ]
 * [ 0,               0,              0,             1 ]
 */
#if geo_matrix_simd_enable
  const SimdVec q  = simd_vec_load(quat.comps);
  const SimdVec q0 = simd_vec_add(q, q);
  SimdVec       q1 = simd_vec_mul(q, q0);

  SimdVec v0 = simd_vec_permute(q1, 3, 0, 0, 1);
  v0         = simd_vec_clear_w(v0);
  SimdVec v1 = simd_vec_permute(q1, 3, 1, 2, 2);
  v1         = simd_vec_clear_w(v1);
  SimdVec r0 = simd_vec_sub(simd_vec_set(1, 1, 1, 0), v0);
  r0         = simd_vec_sub(r0, v1);

  v0 = simd_vec_permute(q, 3, 1, 0, 0);
  v1 = simd_vec_permute(q0, 3, 2, 1, 2);
  v0 = simd_vec_mul(v0, v1);

  v1               = simd_vec_permute(q, 3, 3, 3, 3);
  const SimdVec v2 = simd_vec_permute(q0, 3, 0, 2, 1);
  v1               = simd_vec_mul(v1, v2);

  const SimdVec r1 = simd_vec_add(v0, v1);
  const SimdVec r2 = simd_vec_sub(v0, v1);

  v0 = simd_vec_shuffle(r1, r2, 1, 0, 2, 1);
  v0 = simd_vec_permute(v0, 1, 3, 2, 0);
  v1 = simd_vec_shuffle(r1, r2, 2, 2, 0, 0);
  v1 = simd_vec_permute(v1, 2, 0, 2, 0);

  q1 = simd_vec_shuffle(r0, v0, 1, 0, 3, 0);
  q1 = simd_vec_permute(q1, 1, 3, 2, 0);

  GeoMatrix res;
  simd_vec_store(q1, res.columns[0].comps);

  q1 = simd_vec_shuffle(r0, v0, 3, 2, 3, 1);
  q1 = simd_vec_permute(q1, 1, 3, 0, 2);
  simd_vec_store(q1, res.columns[1].comps);

  q1 = simd_vec_shuffle(v1, r0, 3, 2, 1, 0);
  simd_vec_store(q1, res.columns[2].comps);
  res.columns[3] = geo_vector(0, 0, 0, 1);
  return res;
#else
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
#endif
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
    const f32 s = intrinsic_sqrt_f32(trace + 1) * 2;
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
    const f32 s = intrinsic_sqrt_f32(1 + m->columns[0].x - m->columns[1].y - m->columns[2].z) * 2;
    return (GeoQuat){
        .x = s * .25f,
        .y = (m->columns[1].x + m->columns[0].y) / s,
        .z = (m->columns[2].x + m->columns[0].z) / s,
        .w = (m->columns[1].z - m->columns[2].y) / s,
    };
  }

  if (m->columns[1].y > m->columns[2].z) {
    // [1, 1] is the biggest.
    const f32 s = intrinsic_sqrt_f32(1 + m->columns[1].y - m->columns[0].x - m->columns[2].z) * 2;
    return (GeoQuat){
        .x = (m->columns[1].x + m->columns[0].y) / s,
        .y = s * .25f,
        .z = (m->columns[2].y + m->columns[1].z) / s,
        .w = (m->columns[2].x - m->columns[0].z) / s,
    };
  }

  // [2, 2] is the biggest.
  const f32 s = intrinsic_sqrt_f32(1 + m->columns[2].z - m->columns[0].x - m->columns[1].y) * 2;
  return (GeoQuat){
      .x = (m->columns[2].x + m->columns[0].z) / s,
      .y = (m->columns[2].y + m->columns[1].z) / s,
      .z = s * .25f,
      .w = (m->columns[0].y - m->columns[1].x) / s,
  };
}

GeoMatrix geo_matrix_trs(const GeoVector t, const GeoQuat r, const GeoVector s) {
#if geo_matrix_simd_enable
  /**
   * Inlined geo_matrix_from_quat() with added scaling and translation.
   */
  const SimdVec q  = simd_vec_load(r.comps);
  const SimdVec q0 = simd_vec_add(q, q);
  SimdVec       q1 = simd_vec_mul(q, q0);

  SimdVec v0 = simd_vec_permute(q1, 3, 0, 0, 1);
  v0         = simd_vec_clear_w(v0);
  SimdVec v1 = simd_vec_permute(q1, 3, 1, 2, 2);
  v1         = simd_vec_clear_w(v1);
  SimdVec r0 = simd_vec_sub(simd_vec_set(1, 1, 1, 0), v0);
  r0         = simd_vec_sub(r0, v1);

  v0 = simd_vec_permute(q, 3, 1, 0, 0);
  v1 = simd_vec_permute(q0, 3, 2, 1, 2);
  v0 = simd_vec_mul(v0, v1);

  v1               = simd_vec_permute(q, 3, 3, 3, 3);
  const SimdVec v2 = simd_vec_permute(q0, 3, 0, 2, 1);
  v1               = simd_vec_mul(v1, v2);

  const SimdVec r1 = simd_vec_add(v0, v1);
  const SimdVec r2 = simd_vec_sub(v0, v1);

  v0 = simd_vec_shuffle(r1, r2, 1, 0, 2, 1);
  v0 = simd_vec_permute(v0, 1, 3, 2, 0);
  v1 = simd_vec_shuffle(r1, r2, 2, 2, 0, 0);
  v1 = simd_vec_permute(v1, 2, 0, 2, 0);

  q1 = simd_vec_shuffle(r0, v0, 1, 0, 3, 0);
  q1 = simd_vec_permute(q1, 1, 3, 2, 0);

  GeoMatrix res;
  simd_vec_store(simd_vec_mul(q1, simd_vec_broadcast(s.x)), res.columns[0].comps);

  q1 = simd_vec_shuffle(r0, v0, 3, 2, 3, 1);
  q1 = simd_vec_permute(q1, 1, 3, 0, 2);
  simd_vec_store(simd_vec_mul(q1, simd_vec_broadcast(s.y)), res.columns[1].comps);

  q1 = simd_vec_shuffle(v1, r0, 3, 2, 1, 0);
  simd_vec_store(simd_vec_mul(q1, simd_vec_broadcast(s.z)), res.columns[2].comps);
  simd_vec_store(simd_vec_w_one(simd_vec_load(t.comps)), res.columns[3].comps);
  return res;
#else
  GeoMatrix res = geo_matrix_from_quat(r);

  // Apply scale.
  for (usize col = 0; col != 3; ++col) {
    res.columns[col].x = res.columns[col].x * s.comps[col];
    res.columns[col].y = res.columns[col].y * s.comps[col];
    res.columns[col].z = res.columns[col].z * s.comps[col];
  }

  // Apply translation.
  res.columns[3].x = t.x;
  res.columns[3].y = t.y;
  res.columns[3].z = t.z;

  return res;
#endif
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
          {1 / intrinsic_tan_f32(horAngle * .5f), 0, 0, 0},
          {0, -(1 / intrinsic_tan_f32(verAngle * .5f)), 0, 0},
          {0, 0, 0, 1},
          {0, 0, zNear, 0},
      }};
}

GeoMatrix geo_matrix_proj_pers_ver(const f32 verAngle, const f32 aspect, const f32 zNear) {
  const f32 horAngle = intrinsic_atan_f32(intrinsic_tan_f32(verAngle * .5f) * aspect) * 2.f;
  return geo_matrix_proj_pers(horAngle, verAngle, zNear);
}

GeoMatrix geo_matrix_proj_pers_hor(const f32 horAngle, const f32 aspect, const f32 zNear) {
  const f32 verAngle = intrinsic_atan_f32(intrinsic_tan_f32(horAngle * .5f) / aspect) * 2.f;
  return geo_matrix_proj_pers(horAngle, verAngle, zNear);
}

void geo_matrix_frustum4(const GeoMatrix* viewProj, GeoPlane out[4]) {
  /**
   * Extract the frustum planes from the viewProj matrix.
   * More information: http://www.cs.otago.ac.nz/postgrads/alexis/planeExtraction.pdf
   */

  // Left clipping plane.
  out[0] = (GeoPlane){
      .normal.x = viewProj->columns[0].w + viewProj->columns[0].x,
      .normal.y = viewProj->columns[1].w + viewProj->columns[1].x,
      .normal.z = viewProj->columns[2].w + viewProj->columns[2].x,
      .distance = -(viewProj->columns[3].w + viewProj->columns[3].x),
  };

  // Right clipping plane.
  out[1] = (GeoPlane){
      .normal.x = viewProj->columns[0].w - viewProj->columns[0].x,
      .normal.y = viewProj->columns[1].w - viewProj->columns[1].x,
      .normal.z = viewProj->columns[2].w - viewProj->columns[2].x,
      .distance = -(viewProj->columns[3].w - viewProj->columns[3].x),
  };

  // Top clipping plane.
  out[2] = (GeoPlane){
      .normal.x = viewProj->columns[0].w - viewProj->columns[0].y,
      .normal.y = viewProj->columns[1].w - viewProj->columns[1].y,
      .normal.z = viewProj->columns[2].w - viewProj->columns[2].y,
      .distance = -(viewProj->columns[3].w - viewProj->columns[3].y),
  };

  // Bottom clipping plane.
  out[3] = (GeoPlane){
      .normal.x = viewProj->columns[0].w + viewProj->columns[0].y,
      .normal.y = viewProj->columns[1].w + viewProj->columns[1].y,
      .normal.z = viewProj->columns[2].w + viewProj->columns[2].y,
      .distance = -(viewProj->columns[3].w + viewProj->columns[3].y),
  };

  // Normalize the planes.
  for (usize i = 0; i != 4; ++i) {
    const f32 mag = geo_vector_mag(out[i].normal);
    out[i].normal = geo_vector_div(out[i].normal, mag);
    out[i].distance /= mag;
  }
}
