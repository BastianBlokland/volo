#include "core_diag.h"
#include "core_math.h"
#include "geo_matrix.h"
#include "geo_quat.h"
#include "geo_vector.h"

#define geo_vec_simd_enable 1

#if geo_vec_simd_enable
#include "simd_sse_internal.h"
#endif

GeoQuat geo_quat_angle_axis(const GeoVector axis, const f32 angle) {
  // TODO: Should we instaed just add the pre-condition that the axis should be a unit vector?
  const f32 axisMag = geo_vector_mag(axis);
  if (axisMag <= f32_epsilon) {
    return geo_quat_ident;
  }
  const GeoVector unitVecAxis = geo_vector_div(axis, axisMag);
  const GeoVector vec         = geo_vector_mul(unitVecAxis, math_sin_f32(angle * .5f));
  return (GeoQuat){vec.x, vec.y, vec.z, math_cos_f32(angle * .5f)};
}

GeoQuat geo_quat_from_to(const GeoQuat from, const GeoQuat to) {
  GeoQuat toIdentity = geo_quat_inv(from);
  return geo_quat_mul(to, toIdentity);
}

f32 geo_quat_angle(const GeoQuat q) {
  const GeoVector axis    = geo_vector(q.x, q.y, q.z);
  const f32       axisMag = geo_vector_mag(axis);
  return 2 * math_atan2_f32(axisMag, q.w);
}

GeoQuat geo_quat_mul(const GeoQuat a, const GeoQuat b) {
#if geo_vec_simd_enable
  GeoQuat res;
  simd_vec_store(simd_vec_qmul(simd_vec_load(a.comps), simd_vec_load(b.comps)), res.comps);
  return res;
#else
  return (GeoQuat){
      .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      .y = a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
      .z = a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
      .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
  };
#endif
}

GeoVector geo_quat_rotate(const GeoQuat q, const GeoVector v) {
#if geo_vec_simd_enable
  const SimdVec quat       = simd_vec_load(q.comps);
  const SimdVec scalar     = simd_vec_splat(quat, 3);
  const SimdVec two        = simd_vec_broadcast(2.0f);
  const SimdVec scalar2    = simd_vec_mul(scalar, two);
  const SimdVec vec        = simd_vec_load(v.comps);
  const SimdVec axis       = simd_vec_clear_w(quat);
  const SimdVec axisSqrMag = simd_vec_dot4(axis, axis);
  const SimdVec dot        = simd_vec_dot4(axis, vec);

  const SimdVec a = simd_vec_mul(axis, simd_vec_mul(dot, two));
  const SimdVec b = simd_vec_mul(vec, simd_vec_sub(simd_vec_mul(scalar, scalar), axisSqrMag));
  const SimdVec c = simd_vec_mul(simd_vec_cross3(axis, vec), scalar2);

  GeoVector res;
  simd_vec_store(simd_vec_add(simd_vec_add(a, b), c), res.comps);
  return res;
#else
  const GeoVector axis       = {q.x, q.y, q.z};
  const f32       axisSqrMag = geo_vector_mag_sqr(axis);
  const f32       scalar     = q.w;
  const GeoVector a          = geo_vector_mul(axis, geo_vector_dot(axis, v) * 2);
  const GeoVector b          = geo_vector_mul(v, scalar * scalar - axisSqrMag);
  const GeoVector c          = geo_vector_mul(geo_vector_cross3(axis, v), scalar * 2);
  return geo_vector_add(geo_vector_add(a, b), c);
#endif
}

GeoQuat geo_quat_inv(const GeoQuat q) {
  // Compute the conjugate ('transposing').
  GeoQuat res = {
      .x = q.x * -1,
      .y = q.y * -1,
      .z = q.z * -1,
      .w = q.w,
  };

  // Divide by the squared length.
  // TODO: Should we just skip this? Is only needed for non-normalized quaternions.
  const f32 sqrMag = geo_vector_mag_sqr((GeoVector){q.x, q.y, q.z, q.w});
  res.x /= sqrMag;
  res.y /= sqrMag;
  res.z /= sqrMag;
  res.w /= sqrMag;
  return res;
}

GeoQuat geo_quat_norm(const GeoQuat q) {
  const f32 mag = geo_vector_mag((GeoVector){q.x, q.y, q.z, q.w});
  diag_assert(mag != 0);
  return (GeoQuat){q.x / mag, q.y / mag, q.z / mag, q.w / mag};
}

GeoQuat geo_quat_look(const GeoVector forward, const GeoVector upRef) {
  const GeoMatrix m = geo_matrix_rotate_look(forward, upRef);
  return geo_matrix_to_quat(&m);
}
