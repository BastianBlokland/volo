#include "core_diag.h"
#include "core_float.h"
#include "core_intrinsic.h"
#include "core_math.h"
#include "geo_matrix.h"
#include "geo_quat.h"
#include "geo_vector.h"

#define geo_quat_simd_enable 1

#if geo_quat_simd_enable
#include "core_simd.h"
#endif

MAYBE_UNUSED static void assert_normalized(const GeoVector v) {
  MAYBE_UNUSED const f32 sqrMag = geo_vector_mag_sqr(v);
  diag_assert_msg(math_abs(sqrMag - 1) < 1e-4, "Given vector is not normalized");
}

GeoQuat geo_quat_angle_axis(const f32 angle, const GeoVector axis) {
#ifndef VOLO_FAST
  assert_normalized(axis);
#endif

#if geo_quat_simd_enable
  const SimdVec angleVec     = simd_vec_broadcast(angle);
  const SimdVec angleHalfVec = simd_vec_mul(angleVec, simd_vec_broadcast(0.5f));

  SimdVec sinVec, cosVec;
  simd_vec_sincos(angleHalfVec, &sinVec, &cosVec);

  const SimdVec sinAxisVec = simd_vec_mul(sinVec, simd_vec_load(axis.comps));

  GeoQuat res;
  simd_vec_store(simd_vec_copy_w(sinAxisVec, cosVec), res.comps);
  return res;
#else
  const GeoVector vec = geo_vector_mul(axis, intrinsic_sin_f32(angle * .5f));
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
#if geo_quat_simd_enable
  GeoQuat res;
  simd_vec_store(simd_quat_conjugate(simd_vec_load(q.comps)), res.comps);
  return res;
#else
  return (GeoQuat){.x = q.x * -1, .y = q.y * -1, .z = q.z * -1, .w = q.w};
#endif
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

GeoQuat geo_quat_norm_or_ident(const GeoQuat q) {
#if geo_quat_simd_enable
  const SimdVec qVec   = simd_vec_load(q.comps);
  const SimdVec magSqr = simd_vec_dot4(qVec, qVec);
  if (simd_vec_x(magSqr) < f32_epsilon) {
    // TODO: Can we avoid this branch?
    return geo_quat_ident;
  }
  GeoQuat res;
  simd_vec_store(simd_vec_mul(qVec, simd_vec_rsqrt(magSqr)), res.comps);
  return res;
#else
  const f32 magSqr = geo_quat_dot(q, q);
  if (magSqr < f32_epsilon) {
    return geo_quat_ident;
  }
  const f32 mag = intrinsic_sqrt_f32(magSqr);
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
   * https://zeux.io/2016/05/05/optimizing-slerp/
   */

#if geo_quat_simd_enable
  // Implementation of Zeux's onlerp.
  const SimdVec l          = simd_vec_load(a.comps);
  const SimdVec r          = simd_vec_load(b.comps);
  const SimdVec vT         = simd_vec_broadcast(t);
  const SimdVec tMinusOne  = simd_vec_sub(vT, simd_vec_broadcast(1.0f));
  const SimdVec tMinusHalf = simd_vec_sub(vT, simd_vec_broadcast(0.5f));

  const SimdVec dot  = simd_vec_dot4(l, r);
  const SimdVec sign = simd_vec_sign(dot);
  const SimdVec d    = simd_vec_xor(dot, sign);

  const SimdVec c0 = simd_vec_broadcast(1.0904f);
  const SimdVec c1 = simd_vec_broadcast(-3.2452f);
  const SimdVec c2 = simd_vec_broadcast(3.55645f);
  const SimdVec c3 = simd_vec_broadcast(1.43519f);
  const SimdVec c4 = simd_vec_broadcast(0.848013f);
  const SimdVec c5 = simd_vec_broadcast(-1.06021f);
  const SimdVec c6 = simd_vec_broadcast(0.215638f);

  const SimdVec vA0 = simd_vec_add(c1, simd_vec_mul(d, simd_vec_sub(c2, simd_vec_mul(d, c3))));
  const SimdVec vA  = simd_vec_add(c0, simd_vec_mul(d, vA0));

  const SimdVec vB0 = simd_vec_add(c5, simd_vec_mul(d, c6));
  const SimdVec vB  = simd_vec_add(c4, simd_vec_mul(d, vB0));

  const SimdVec vK = simd_vec_add(simd_vec_mul(vA, simd_vec_mul(tMinusHalf, tMinusHalf)), vB);

  const SimdVec vOT0 = simd_vec_mul(vT, simd_vec_mul(tMinusHalf, tMinusOne));
  const SimdVec vOT  = simd_vec_add(vT, simd_vec_mul(vOT0, vK));

  const SimdVec rScaled = simd_vec_mul(simd_vec_xor(vOT, sign), r);
  const SimdVec lScaled = simd_vec_mul(vOT, l);
  const SimdVec vInterp = simd_vec_add(rScaled, simd_vec_sub(l, lScaled));

  GeoQuat res;
  simd_vec_store(simd_quat_norm(vInterp), res.comps);
  return res;
#else
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
#endif
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
  if (axisMagSqr > f32_epsilon) {
    const f32 axisMag = intrinsic_sqrt_f32(axisMagSqr);
    return geo_vector_mul(axis, 2.0f * intrinsic_atan2_f32(axisMag, q.w) / axisMag);
  }
  return geo_vector_mul(axis, 2.0f);
}

f32 geo_quat_to_angle(const GeoQuat q) { return geo_vector_mag(geo_quat_to_angle_axis(q)); }

GeoSwingTwist geo_quat_to_swing_twist(const GeoQuat q, const GeoVector twistAxis) {
#ifndef VOLO_FAST
  assert_normalized(twistAxis);
#endif
  static const f32 g_twistEpsilon = 1e-9f;

  /**
   * Quaternion swing-twist decomposition.
   * Reference: http://allenchou.net/2018/05/game-math-swing-twist-interpolation-sterp/
   * Reference: http://www.euclideanspace.com/maths/geometry/rotations/for/decomposition/
   */
  GeoSwingTwist   result;
  const GeoVector qAxis       = geo_vector(q.x, q.y, q.z);
  const f32       qAxisSqrMag = geo_vector_mag_sqr(qAxis);
  if (qAxisSqrMag < g_twistEpsilon) {
    // Singularity: rotation by 180 degrees.
    const GeoVector rotatedTwistAxis = geo_quat_rotate(q, twistAxis);
    const GeoVector swingAxis        = geo_vector_cross3(twistAxis, rotatedTwistAxis);
    const f32       swingAxisSqrMag  = geo_vector_mag_sqr(swingAxis);
    if (swingAxisSqrMag > g_twistEpsilon) {
      const f32 swingAngle = geo_vector_angle(twistAxis, rotatedTwistAxis);
      result.swing         = geo_quat_angle_axis(swingAngle, swingAxis);
    } else {
      // Singularity: rotation axis parallel to twist axis.
      result.swing = geo_quat_ident;
    }
    result.twist = geo_quat_angle_axis(math_pi_f32, twistAxis);
    return result;
  }
  const GeoVector p = geo_vector_project(qAxis, twistAxis);
  result.twist      = geo_quat_norm_or_ident((GeoQuat){.x = p.x, .y = p.y, .z = p.z, .w = q.w});
  result.swing      = geo_quat_mul(q, geo_quat_inverse(result.twist));
  return result;
}

GeoQuat geo_quat_to_twist(const GeoQuat q, const GeoVector twistAxis) {
#ifndef VOLO_FAST
  assert_normalized(twistAxis);
#endif
  const GeoVector qAxis = geo_vector(q.x, q.y, q.z);
  const GeoVector p     = geo_vector_project(qAxis, twistAxis);
  return geo_quat_norm_or_ident((GeoQuat){.x = p.x, .y = p.y, .z = p.z, .w = q.w});
}

bool geo_quat_clamp(GeoQuat* q, const f32 maxAngle) {
  diag_assert_msg(maxAngle >= 0.0f, "maximum angle cannot be negative");

  const GeoVector angleAxis = geo_quat_to_angle_axis(*q);
  const f32       angleSqr  = geo_vector_mag_sqr(angleAxis);
  if (angleSqr <= (maxAngle * maxAngle)) {
    return false;
  }
  const f32       angle = intrinsic_sqrt_f32(angleSqr);
  const GeoVector axis  = geo_vector_div(angleAxis, angle);

  GeoQuat clamped = geo_quat_angle_axis(math_min(angle, maxAngle), axis);
  if (geo_quat_dot(clamped, *q) < 0) {
    // Compensate for quaternion double-cover (two quaternions representing the same rot).
    clamped = geo_quat_flip(clamped);
  }

  *q = clamped;
  return true;
}

void geo_quat_pack_f16(const GeoQuat quat, f16 out[PARAM_ARRAY_SIZE(4)]) {
#if geo_quat_simd_enable
  if (g_f16cSupport) {
    const SimdVec vecF32 = simd_vec_load(quat.comps);
    const SimdVec vecF16 = simd_vec_f32_to_f16(vecF32);
    const u64     data   = simd_vec_u64(vecF16);
    out[0]               = (u16)(data >> 0);
    out[1]               = (u16)(data >> 16);
    out[2]               = (u16)(data >> 32);
    out[3]               = (u16)(data >> 48);
    return;
  }
#endif
  out[0] = float_f32_to_f16(quat.x);
  out[1] = float_f32_to_f16(quat.y);
  out[2] = float_f32_to_f16(quat.z);
  out[3] = float_f32_to_f16(quat.w);
}
