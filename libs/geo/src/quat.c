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
  const f32 axisMagSqr = geo_vector_mag_sqr(axis);
  if (axisMagSqr <= f32_epsilon) {
    return geo_quat_ident;
  }
  const f32       axisMag     = intrinsic_sqrt_f32(axisMagSqr);
  const GeoVector unitVecAxis = geo_vector_div(axis, axisMag);
  const GeoVector vec         = geo_vector_mul(unitVecAxis, intrinsic_sin_f32(angle * .5f));
  return (GeoQuat){vec.x, vec.y, vec.z, intrinsic_cos_f32(angle * .5f)};
#endif
}

GeoQuat geo_quat_from_to(const GeoQuat from, const GeoQuat to) {
  const GeoQuat toIdentity = geo_quat_inverse(from);
  return geo_quat_mul(to, toIdentity);
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

GeoQuat geo_quat_mul_comps(const GeoQuat a, const GeoVector b) {
#if geo_quat_simd_enable
  GeoQuat res;
  simd_vec_store(simd_vec_mul(simd_vec_load(a.comps), simd_vec_load(b.comps)), res.comps);
  return res;
#else
  return (GeoQuat){
      .x = a.x * b.x,
      .y = a.y * b.y,
      .z = a.z * b.z,
      .w = a.w * b.w,
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

GeoQuat geo_quat_inverse(const GeoQuat q) {
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

GeoQuat geo_quat_flip(const GeoQuat q) {
#if geo_quat_simd_enable
  GeoQuat res;
  simd_vec_store(simd_vec_mul(simd_vec_load(q.comps), simd_vec_broadcast(-1.0f)), res.comps);
  return res;
#else
  return (GeoQuat){
      .x = q.x * -1,
      .y = q.y * -1,
      .z = q.z * -1,
      .w = q.w * -1,
  };
#endif
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

f32 geo_quat_dot(const GeoQuat a, const GeoQuat b) {
#if geo_quat_simd_enable
  return simd_vec_x(simd_vec_dot4(simd_vec_load(a.comps), simd_vec_load(b.comps)));
#else
  f32 res = 0;
  res += a.x * b.x;
  res += a.y * b.y;
  res += a.z * b.z;
  res += a.w * b.w;
  return res;
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

  const f32 dot = geo_quat_dot(a, b);
  f32       tA, tB;

  if (math_abs(dot) < 0.99999f) {
    const f32 x = intrinsic_acos_f32(dot);
    const f32 y = 1.0f / intrinsic_sin_f32(x);

    tA = intrinsic_sin_f32((1.0f - t) * x) * y;
    tB = intrinsic_sin_f32(t * x) * y;
  } else {
    tA = 1.0f - t;
    tB = t;
  }

  return (GeoQuat){
      a.x * tA + b.x * tB,
      a.y * tA + b.y * tB,
      a.z * tA + b.z * tB,
      a.w * tA + b.w * tB,
  };
}

bool geo_quat_towards(GeoQuat* q, const GeoQuat target, const f32 maxAngle) {
  GeoQuat    rotDelta = geo_quat_from_to(*q, target);
  const bool clamped  = geo_quat_clamp(&rotDelta, maxAngle);
  *q                  = geo_quat_mul(rotDelta, *q);
  *q                  = geo_quat_norm(*q);
  return !clamped;
}

GeoQuat geo_quat_from_euler(const GeoVector e) {
  /**
   * Implementation based on:
   * https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
   */

  const f32 cy = intrinsic_cos_f32(e.z * 0.5f);
  const f32 sy = intrinsic_sin_f32(e.z * 0.5f);
  const f32 cp = intrinsic_cos_f32(e.y * 0.5f);
  const f32 sp = intrinsic_sin_f32(e.y * 0.5f);
  const f32 cr = intrinsic_cos_f32(e.x * 0.5f);
  const f32 sr = intrinsic_sin_f32(e.x * 0.5f);

  return (GeoQuat){
      .x = sr * cp * cy - cr * sp * sy,
      .y = cr * sp * cy + sr * cp * sy,
      .z = cr * cp * sy - sr * sp * cy,
      .w = cr * cp * cy + sr * sp * sy,
  };
}

GeoVector geo_quat_to_euler(const GeoQuat q) {
  /**
   * Implementation based on:
   * https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
   */

  const f32 sinrCosp = 2.0f * (q.w * q.x + q.y * q.z);
  const f32 cosrCosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
  const f32 roll     = math_atan2_f32(sinrCosp, cosrCosp);

  const f32 sinp = 2.0f * (q.w * q.y - q.z * q.x);
  f32       pitch;
  if (UNLIKELY(math_abs(sinp) >= 1.0f)) {
    pitch = math_pi_f32 * 0.5f * math_sign(sinp); // Out of range: default to 90 degrees.
  } else {
    pitch = intrinsic_asin_f32(sinp);
  }

  const f32 sinyCosp = 2.0f * (q.w * q.z + q.x * q.y);
  const f32 cosyCosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
  const f32 yaw      = intrinsic_atan2_f32(sinyCosp, cosyCosp);

  return (GeoVector){
      .x = roll,
      .y = pitch,
      .z = yaw,
  };
}

GeoVector geo_quat_to_angle_axis(const GeoQuat q) {
  const GeoVector axis       = geo_vector(q.x, q.y, q.z);
  const f32       axisMagSqr = geo_vector_mag_sqr(axis);
  if (axisMagSqr >= f32_epsilon) {
    const f32 axisMag = intrinsic_sqrt_f32(axisMagSqr);
    return geo_vector_mul(axis, 2.0f * intrinsic_atan2_f32(axisMag, q.w) / axisMag);
  }
  return geo_vector_mul(axis, 2.0f);
}

bool geo_quat_clamp(GeoQuat* q, const f32 maxAngle) {
  const GeoVector angleAxis = geo_quat_to_angle_axis(*q);
  const f32       angleSqr  = geo_vector_mag_sqr(angleAxis);
  if (angleSqr <= (maxAngle * maxAngle)) {
    return false;
  }
  const f32       angle = intrinsic_sqrt_f32(angleSqr);
  const GeoVector axis  = geo_vector_div(angleAxis, angle);

  GeoQuat clamped = geo_quat_angle_axis(axis, math_min(angle, maxAngle));
  if (geo_quat_dot(clamped, *q) < 0) {
    // Compensate for quaternion double-cover (two quaternions representing the same rot).
    clamped = geo_quat_flip(clamped);
  }

  *q = clamped;
  return true;
}

void geo_quat_pack_f16(const GeoQuat quat, f16 out[4]) {
  out[0] = float_f32_to_f16(quat.x);
  out[1] = float_f32_to_f16(quat.y);
  out[2] = float_f32_to_f16(quat.z);
  out[3] = float_f32_to_f16(quat.w);
}
