#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "geo_vector.h"

#include "intrinsic_internal.h"

#define geo_vec_simd_enable 1

#if geo_vec_simd_enable
#include "simd_sse_internal.h"
#endif

bool geo_vector_equal(const GeoVector a, const GeoVector b, const f32 threshold) {
  const GeoVector diff = geo_vector_abs(geo_vector_sub(a, b));
  return diff.x <= threshold && diff.y <= threshold && diff.z <= threshold && diff.w <= threshold;
}

bool geo_vector_equal3(const GeoVector a, const GeoVector b, const f32 threshold) {
  const GeoVector diff = geo_vector_abs(geo_vector_sub(a, b));
  return diff.x <= threshold && diff.y <= threshold && diff.z <= threshold;
}

GeoVector geo_vector_abs(const GeoVector vec) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_abs(simd_vec_load(vec.comps)), res.comps);
  return res;
#else
  return geo_vector(math_abs(vec.x), math_abs(vec.y), math_abs(vec.z), math_abs(vec.w));
#endif
}

GeoVector geo_vector_add(const GeoVector a, const GeoVector b) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_add(simd_vec_load(a.comps), simd_vec_load(b.comps)), res.comps);
  return res;
#else
  return geo_vector(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
#endif
}

GeoVector geo_vector_sub(const GeoVector a, const GeoVector b) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_sub(simd_vec_load(a.comps), simd_vec_load(b.comps)), res.comps);
  return res;
#else
  return geo_vector(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
#endif
}

GeoVector geo_vector_mul(const GeoVector v, const f32 scalar) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_mul(simd_vec_load(v.comps), simd_vec_broadcast(scalar)), res.comps);
  return res;
#else
  return geo_vector(v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar);
#endif
}

GeoVector geo_vector_mul_comps(const GeoVector a, const GeoVector b) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_mul(simd_vec_load(a.comps), simd_vec_load(b.comps)), res.comps);
  return res;
#else
  return geo_vector(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
#endif
}

GeoVector geo_vector_div(const GeoVector v, const f32 scalar) {
  diag_assert(scalar != 0);
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_div(simd_vec_load(v.comps), simd_vec_broadcast(scalar)), res.comps);
  return res;
#else
  return geo_vector(v.x / scalar, v.y / scalar, v.z / scalar, v.w / scalar);
#endif
}

GeoVector geo_vector_div_comps(const GeoVector a, const GeoVector b) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_div(simd_vec_load(a.comps), simd_vec_load(b.comps)), res.comps);
  return res;
#else
  return geo_vector(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w);
#endif
}

f32 geo_vector_mag_sqr(const GeoVector v) {
#if geo_vec_simd_enable
  const SimdVec tmp = simd_vec_load(v.comps);
  return simd_vec_x(simd_vec_dot4(tmp, tmp));
#else
  return geo_vector_dot(v, v);
#endif
}

f32 geo_vector_mag(const GeoVector v) {
#if geo_vec_simd_enable
  const SimdVec tmp = simd_vec_load(v.comps);
  const SimdVec dot = simd_vec_dot4(tmp, tmp);
  return simd_vec_x(dot) != 0 ? simd_vec_x(simd_vec_sqrt(dot)) : 0;
#else
  const f32 sqrMag = geo_vector_mag_sqr(v);
  return sqrMag != 0 ? intrinsic_sqrt_f32(sqrMag) : 0;
#endif
}

GeoVector geo_vector_norm(const GeoVector v) {
  const f32 mag = geo_vector_mag(v);
  diag_assert(mag != 0);
  return geo_vector_div(v, mag);
}

f32 geo_vector_dot(const GeoVector a, const GeoVector b) {
#if geo_vec_simd_enable
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

GeoVector geo_vector_cross3(const GeoVector a, const GeoVector b) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_cross3(simd_vec_load(a.comps), simd_vec_load(b.comps)), res.comps);
  return res;
#else
  const f32 x = a.y * b.z - a.z * b.y;
  const f32 y = a.z * b.x - a.x * b.z;
  const f32 z = a.x * b.y - a.y * b.x;
  return geo_vector(x, y, z);
#endif
}

f32 geo_vector_angle(const GeoVector from, const GeoVector to) {
  const f32 denom = intrinsic_sqrt_f32(geo_vector_mag_sqr(from) * geo_vector_mag_sqr(to));
  if (denom <= f32_epsilon) {
    return 0;
  }
  const f32 dot = geo_vector_dot(from, to);
  return intrinsic_acos_f32(math_clamp_f32(dot / denom, -1, 1));
}

GeoVector geo_vector_project(const GeoVector v, const GeoVector nrm) {
  const f32 nrmSqrMag = geo_vector_mag_sqr(nrm);
  if (nrmSqrMag <= f32_epsilon) {
    return geo_vector(0);
  }
  return geo_vector_mul(nrm, geo_vector_dot(v, nrm) / nrmSqrMag);
}

GeoVector geo_vector_reflect(const GeoVector v, const GeoVector nrm) {
  const f32 dot = geo_vector_dot(v, nrm);
  return geo_vector_sub(v, geo_vector_mul(nrm, dot * 2));
}

GeoVector geo_vector_lerp(const GeoVector x, const GeoVector y, const f32 t) {
#if geo_vec_simd_enable
  const SimdVec vX = simd_vec_load(x.comps);
  const SimdVec vY = simd_vec_load(y.comps);
  const SimdVec vT = simd_vec_broadcast(t);
  GeoVector     res;
  simd_vec_store(simd_vec_add(vX, simd_vec_mul(simd_vec_sub(vY, vX), vT)), res.comps);
  return res;
#else
  return (GeoVector){
      .x = math_lerp(x.x, y.x, t),
      .y = math_lerp(x.y, y.y, t),
      .z = math_lerp(x.z, y.z, t),
      .w = math_lerp(x.w, y.w, t),
  };
#endif
}

GeoVector geo_vector_min(const GeoVector x, const GeoVector y) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_min(simd_vec_load(x.comps), simd_vec_load(y.comps)), res.comps);
  return res;
#else
  return (GeoVector){
      .x = math_min(x.x, y.x),
      .y = math_min(x.y, y.y),
      .z = math_min(x.z, y.z),
      .w = math_min(x.w, y.w),
  };
#endif
}

GeoVector geo_vector_max(const GeoVector x, const GeoVector y) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_max(simd_vec_load(x.comps), simd_vec_load(y.comps)), res.comps);
  return res;
#else
  return (GeoVector){
      .x = math_max(x.x, y.x),
      .y = math_max(x.y, y.y),
      .z = math_max(x.z, y.z),
      .w = math_max(x.w, y.w),
  };
#endif
}

GeoVector geo_vector_xyz(const GeoVector v) { return geo_vector(v.x, v.y, v.z, 0); }
GeoVector geo_vector_xz(const GeoVector v) { return geo_vector(v.x, 0, v.z, 0); }

GeoVector geo_vector_sqrt(const GeoVector v) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_sqrt(simd_vec_load(v.comps)), res.comps);
  return res;
#else
  return geo_vector(
      intrinsic_sqrt_f32(v.x),
      intrinsic_sqrt_f32(v.y),
      intrinsic_sqrt_f32(v.z),
      intrinsic_sqrt_f32(v.w));
#endif
}

GeoVector geo_vector_clamp(const GeoVector v, const f32 maxMagnitude) {
  diag_assert_msg(maxMagnitude >= 0.0f, "maximum magnitude cannot be negative");

  const f32 sqrMag = geo_vector_mag_sqr(v);
  if (sqrMag > (maxMagnitude * maxMagnitude)) {
    const GeoVector norm = geo_vector_div(v, intrinsic_sqrt_f32(sqrMag));
    return geo_vector_mul(norm, maxMagnitude);
  }
  return v;
}

GeoVector geo_vector_round_nearest(const GeoVector v) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_round_nearest(simd_vec_load(v.comps)), res.comps);
  return res;
#else
  return (GeoVector){
      .x = intrinsic_round_f32(v.x),
      .y = intrinsic_round_f32(v.y),
      .z = intrinsic_round_f32(v.z),
      .w = intrinsic_round_f32(v.w),
  };
#endif
}

GeoVector geo_vector_round_down(const GeoVector v) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_round_down(simd_vec_load(v.comps)), res.comps);
  return res;
#else
  return (GeoVector){
      .x = intrinsic_floor_f32(v.x),
      .y = intrinsic_floor_f32(v.y),
      .z = intrinsic_floor_f32(v.z),
      .w = intrinsic_floor_f32(v.w),
  };
#endif
}

GeoVector geo_vector_round_up(const GeoVector v) {
#if geo_vec_simd_enable
  GeoVector res;
  simd_vec_store(simd_vec_round_up(simd_vec_load(v.comps)), res.comps);
  return res;
#else
  return (GeoVector){
      .x = intrinsic_ceil_f32(v.x),
      .y = intrinsic_ceil_f32(v.y),
      .z = intrinsic_ceil_f32(v.z),
      .w = intrinsic_ceil_f32(v.w),
  };
#endif
}

GeoVector geo_vector_perspective_div(const GeoVector v) {
  return geo_vector_div(geo_vector(v.x, v.y, v.z), v.w);
}

GeoVector geo_vector_quantize(const GeoVector v, const u8 maxMantissaBits) {
  return (GeoVector){
      .x = float_quantize_f32(v.x, maxMantissaBits),
      .y = float_quantize_f32(v.y, maxMantissaBits),
      .z = float_quantize_f32(v.z, maxMantissaBits),
      .w = float_quantize_f32(v.w, maxMantissaBits),
  };
}

GeoVector geo_vector_quantize3(const GeoVector v, const u8 maxMantissaBits) {
  return (GeoVector){
      .x = float_quantize_f32(v.x, maxMantissaBits),
      .y = float_quantize_f32(v.y, maxMantissaBits),
      .z = float_quantize_f32(v.z, maxMantissaBits),
  };
}

void geo_vector_pack_f16(const GeoVector v, f16 out[4]) {
  out[0] = float_f32_to_f16(v.x);
  out[1] = float_f32_to_f16(v.y);
  out[2] = float_f32_to_f16(v.z);
  out[3] = float_f32_to_f16(v.w);
}
