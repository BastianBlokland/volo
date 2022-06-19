#include "core_diag.h"
#include "core_math.h"
#include "geo_matrix.h"
#include "geo_quat.h"
#include "geo_vector.h"

#include "intrinsic_internal.h"

#define geo_quat_simd_enable 1

#if geo_quat_simd_enable
#include "simd_sse_internal.h"
#endif

GeoQuat geo_quat_angle_axis(const GeoVector axis, const f32 angle) {
  /**
   * TODO: Should we add the pre-condition that the axis should be a unit vector?
   */
#if geo_quat_simd_enable
  const SimdVec axisVec    = simd_vec_load(axis.comps);
  const SimdVec axisSqrMag = simd_vec_dot4(axisVec, axisVec);
  if (simd_vec_x(axisSqrMag) <= f32_epsilon) {
    return geo_quat_ident;
  }
  const SimdVec axisMag      = simd_vec_sqrt(axisSqrMag);
  const SimdVec axisUnit     = simd_vec_div(axisVec, axisMag);
  const f32     halfHandle   = angle * 0.5f;
  const f32     sinHalfAngle = intrinsic_sin_f32(halfHandle);
  const f32     cosHalfAngle = intrinsic_cos_f32(halfHandle);
  GeoQuat       res;
  simd_vec_store(simd_vec_mul(axisUnit, simd_vec_broadcast(sinHalfAngle)), res.comps);
  res.w = cosHalfAngle;
  return res;
#else
  const f32 axisMag = geo_vector_mag(axis);
  if (axisMag <= f32_epsilon) {
    return geo_quat_ident;
  }
  const GeoVector unitVecAxis = geo_vector_div(axis, axisMag);
  const GeoVector vec         = geo_vector_mul(unitVecAxis, intrinsic_sin_f32(angle * .5f));
  return (GeoQuat){vec.x, vec.y, vec.z, intrinsic_cos_f32(angle * .5f)};
#endif
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
#if geo_quat_simd_enable
  GeoQuat res;
  simd_vec_store(simd_quat_mul(simd_vec_load(a.comps), simd_vec_load(b.comps)), res.comps);
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
#if geo_quat_simd_enable
  GeoVector res;
  simd_vec_store(simd_quat_rotate(simd_vec_load(q.comps), simd_vec_load(v.comps)), res.comps);
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
#if geo_quat_simd_enable
  GeoQuat res;
  simd_vec_store(simd_quat_norm(simd_vec_load(q.comps)), res.comps);
  return res;
#else
  const f32 mag = geo_vector_mag((GeoVector){q.x, q.y, q.z, q.w});
  diag_assert(mag != 0);
  return (GeoQuat){q.x / mag, q.y / mag, q.z / mag, q.w / mag};
#endif
}

GeoQuat geo_quat_look(const GeoVector forward, const GeoVector upRef) {
  const GeoMatrix m = geo_matrix_rotate_look(forward, upRef);
  return geo_matrix_to_quat(&m);
}

GeoQuat geo_quat_slerp(const GeoQuat a, const GeoQuat b, const f32 t) {
  /**
   * Walk from one quaternion to another along the unit sphere in 4-dimensional space.
   *
   * Implementation based on:
   * https://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/slerp
   */

  // Calculate the angle between the quaternions.
  const f32 cosHalfTheta = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  // if a == b or a == -b then theta == 0 and we can return a
  if (math_abs(cosHalfTheta) >= 1.0f) {
    return a;
  }

  const f32 halfTheta    = math_acos_f32(cosHalfTheta);
  const f32 sinHalfTheta = math_sqrt_f32(1.0f - cosHalfTheta * cosHalfTheta);
  // if theta == 180 degrees then result is not fully defined
  // we could rotate around any axis normal to a or b
  if (math_abs(sinHalfTheta) < 0.001f) {
    return (GeoQuat){
        .x = a.x * 0.5f + b.x * 0.5f,
        .y = a.y * 0.5f + b.y * 0.5f,
        .z = a.z * 0.5f + b.z * 0.5f,
        .w = a.w * 0.5f + b.w * 0.5f,
    };
  }

  const f32 ratioA = math_sin_f32((1.0f - t) * halfTheta) / sinHalfTheta;
  const f32 ratioB = math_sin_f32(t * halfTheta) / sinHalfTheta;
  return (GeoQuat){
      .w = a.w * ratioA + b.w * ratioB,
      .x = a.x * ratioA + b.x * ratioB,
      .y = a.y * ratioA + b.y * ratioB,
      .z = a.z * ratioA + b.z * ratioB,
  };
}
