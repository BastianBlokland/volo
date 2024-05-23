#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_intrinsic.h"
#include "core_math.h"
#include "geo_color.h"

#define geo_color_simd_enable 1

#if geo_color_simd_enable
#include "core_simd.h"
#endif

GeoColor geo_color_for(const u64 idx) {
  const u32 hash = bits_hash_32(mem_var(idx));
  return geo_color_for_hash(hash);
}

GeoColor geo_color_for_hash(const u32 hash) {
  static const f32 g_u32MaxInv = 1.0f / u32_max;
  const f32        hue         = (f32)hash * g_u32MaxInv;
  const f32        saturation  = 1.0f;
  const f32        value       = 1.0f;
  const f32        alpha       = 1.0f;
  return geo_color_from_hsv(hue, saturation, value, alpha);
}

bool geo_color_equal(const GeoColor a, const GeoColor b, const f32 threshold) {
  const GeoColor diff = geo_color_abs(geo_color_sub(a, b));
  return diff.r <= threshold && diff.g <= threshold && diff.b <= threshold && diff.a <= threshold;
}

GeoColor geo_color_abs(const GeoColor c) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_abs(simd_vec_load(c.data)), res.data);
  return res;
#else
  return geo_color(math_abs(c.r), math_abs(c.g), math_abs(c.b), math_abs(c.a));
#endif
}

GeoColor geo_color_add(const GeoColor a, const GeoColor b) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_add(simd_vec_load(a.data), simd_vec_load(b.data)), res.data);
  return res;
#else
  return geo_color(a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a);
#endif
}

GeoColor geo_color_sub(const GeoColor a, const GeoColor b) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_sub(simd_vec_load(a.data), simd_vec_load(b.data)), res.data);
  return res;
#else
  return geo_color(a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a);
#endif
}

GeoColor geo_color_mul(const GeoColor c, const f32 scalar) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_mul(simd_vec_load(c.data), simd_vec_broadcast(scalar)), res.data);
  return res;
#else
  return geo_color(c.r * scalar, c.g * scalar, c.b * scalar, c.a * scalar);
#endif
}

GeoColor geo_color_mul_comps(const GeoColor a, const GeoColor b) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_mul(simd_vec_load(a.data), simd_vec_load(b.data)), res.data);
  return res;
#else
  return geo_color(a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a);
#endif
}

GeoColor geo_color_div(const GeoColor c, const f32 scalar) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_div(simd_vec_load(c.data), simd_vec_broadcast(scalar)), res.data);
  return res;
#else
  const f32 scalarInv = 1.0f / scalar;
  return geo_color(c.r * scalarInv, c.g * scalarInv, c.b * scalarInv, c.a * scalarInv);
#endif
}

GeoColor geo_color_div_comps(const GeoColor a, const GeoColor b) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_div(simd_vec_load(a.data), simd_vec_load(b.data)), res.data);
  return res;
#else
  return geo_color(a.r / b.r, a.g / b.g, a.b / b.b, a.a / b.a);
#endif
}

f32 geo_color_mag(const GeoColor c) {
#if geo_color_simd_enable
  const SimdVec tmp = simd_vec_load(c.data);
  const SimdVec dot = simd_vec_dot4(tmp, tmp);
  return simd_vec_x(dot) != 0 ? simd_vec_x(simd_vec_sqrt(dot)) : 0;
#else
  f32 sqrMag = 0;
  sqrMag += c.r * c.r;
  sqrMag += c.g * c.g;
  sqrMag += c.b * c.b;
  sqrMag += c.a * c.a;
  return sqrMag != 0 ? intrinsic_sqrt_f32(sqrMag) : 0;
#endif
}

GeoColor geo_color_lerp(const GeoColor x, const GeoColor y, const f32 t) {
#if geo_color_simd_enable
  const SimdVec vX = simd_vec_load(x.data);
  const SimdVec vY = simd_vec_load(y.data);
  const SimdVec vT = simd_vec_broadcast(t);
  GeoColor      res;
  simd_vec_store(simd_vec_add(vX, simd_vec_mul(simd_vec_sub(vY, vX), vT)), res.data);
  return res;
#else
  return geo_color(
      math_lerp(x.r, y.r, t),
      math_lerp(x.g, y.g, t),
      math_lerp(x.b, y.b, t),
      math_lerp(x.a, y.a, t));
#endif
}

GeoColor geo_color_bilerp(
    const GeoColor c1,
    const GeoColor c2,
    const GeoColor c3,
    const GeoColor c4,
    const f32      tX,
    const f32      tY) {
#if geo_color_simd_enable
  const SimdVec vec1  = simd_vec_load(c1.data);
  const SimdVec vec2  = simd_vec_load(c2.data);
  const SimdVec vec3  = simd_vec_load(c3.data);
  const SimdVec vec4  = simd_vec_load(c4.data);
  const SimdVec vecTX = simd_vec_broadcast(tX);
  const SimdVec vecTY = simd_vec_broadcast(tY);
  const SimdVec tmp1  = simd_vec_add(vec1, simd_vec_mul(simd_vec_sub(vec2, vec1), vecTX));
  const SimdVec tmp2  = simd_vec_add(vec3, simd_vec_mul(simd_vec_sub(vec4, vec3), vecTX));
  GeoColor      res;
  simd_vec_store(simd_vec_add(tmp1, simd_vec_mul(simd_vec_sub(tmp2, tmp1), vecTY)), res.data);
  return res;
#else
  return geo_color_lerp(geo_color_lerp(c1, c2, tX), geo_color_lerp(c3, c4, tX), tY);
#endif
}

GeoColor geo_color_min(const GeoColor x, const GeoColor y) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_min(simd_vec_load(x.data), simd_vec_load(y.data)), res.data);
  return res;
#else
  return (GeoColor){
      .r = math_min(x.r, y.r),
      .g = math_min(x.g, y.g),
      .b = math_min(x.b, y.b),
      .a = math_min(x.a, y.a),
  };
#endif
}

GeoColor geo_color_max(const GeoColor x, const GeoColor y) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_max(simd_vec_load(x.data), simd_vec_load(y.data)), res.data);
  return res;
#else
  return (GeoColor){
      .r = math_max(x.r, y.r),
      .g = math_max(x.g, y.g),
      .b = math_max(x.b, y.b),
      .a = math_max(x.a, y.a),
  };
#endif
}

GeoColor geo_color_clamp(const GeoColor c, const f32 maxMagnitude) {
  diag_assert_msg(maxMagnitude >= 0.0f, "maximum magnitude cannot be negative");

  const f32 mag = geo_color_mag(c); // TODO: We can use a square-magnitude for the condition.
  if (mag > maxMagnitude) {
    const GeoColor norm = geo_color_div(c, mag);
    return geo_color_mul(norm, maxMagnitude);
  }
  return c;
}

GeoColor geo_color_clamp_comps(const GeoColor c, const GeoColor min, const GeoColor max) {
#if geo_color_simd_enable
  SimdVec vec = simd_vec_load(c.data);
  vec         = simd_vec_max(vec, simd_vec_load(min.data));
  vec         = simd_vec_min(vec, simd_vec_load(max.data));
  GeoColor res;
  simd_vec_store(vec, res.data);
  return res;
#else
  return (GeoColor){
      .r = math_clamp_f32(c.r, min.r, max.r),
      .g = math_clamp_f32(c.g, min.g, max.g),
      .b = math_clamp_f32(c.b, min.b, max.b),
      .a = math_clamp_f32(c.a, min.a, max.a),
  };
#endif
}

GeoColor geo_color_with_alpha(const GeoColor color, const f32 alpha) {
  return geo_color(color.r, color.g, color.b, alpha);
}

GeoColor geo_color_linear_to_srgb(const GeoColor linear) {
/**
 * Linear to srgb curve approximation.
 * Implementation based on: http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
 */
#if geo_color_simd_enable
  const SimdVec vecLinear = simd_vec_load(linear.data);
  const SimdVec s1        = simd_vec_sqrt(vecLinear);
  const SimdVec s2        = simd_vec_sqrt(s1);
  const SimdVec s3        = simd_vec_sqrt(s2);
  const SimdVec srgb      = simd_vec_sub(
      simd_vec_add(
          simd_vec_mul(s1, simd_vec_broadcast(0.585122381f)),
          simd_vec_mul(s2, simd_vec_broadcast(0.783140355f))),
      simd_vec_mul(s3, simd_vec_broadcast(0.368262736f)));

  GeoColor res;
  simd_vec_store(simd_vec_copy_w(simd_vec_max(srgb, simd_vec_zero()), vecLinear), res.data);
  return res;
#else
  return (GeoColor){
      .r = math_max(1.055f * math_pow_f32(linear.r, 0.416666667f) - 0.055f, 0),
      .g = math_max(1.055f * math_pow_f32(linear.g, 0.416666667f) - 0.055f, 0),
      .b = math_max(1.055f * math_pow_f32(linear.b, 0.416666667f) - 0.055f, 0),
      .a = linear.a,
  };
#endif
}

GeoColor geo_color_srgb_to_linear(const GeoColor srgb) {
  return (GeoColor){
      .r = math_pow_f32(srgb.r, 2.233333333f),
      .g = math_pow_f32(srgb.g, 2.233333333f),
      .b = math_pow_f32(srgb.b, 2.233333333f),
      .a = srgb.a,
  };
}

GeoColor geo_color_from_hsv(const f32 hue, const f32 saturation, const f32 value, const f32 alpha) {
  diag_assert(hue >= 0.0f && hue <= 1.0f);
  diag_assert(saturation >= 0.0f && saturation <= 1.0f);

  /**
   * hsv to rgb, implementation based on:
   * http://ilab.usc.edu/wiki/index.php/HSV_And_H2SV_Color_Space#HSV_Transformation_C_.2F_C.2B.2B_Code_2
   */
  if (value == 0.0f) {
    return geo_color(0, 0, 0, alpha);
  }
  if (saturation == 0.0f) {
    return geo_color(value, value, value, alpha);
  }
  static const f32 g_hueSegInv = 1.0f / (60.0f / 360.0f);
  const f32        hueSeg      = hue * g_hueSegInv;
  const i32        hueIndex    = (i32)intrinsic_round_down_f32(hueSeg);
  const f32        hueFrac     = hueSeg - (f32)hueIndex;
  const f32        pV          = value * (1.0f - saturation);
  const f32        qV          = value * (1.0f - saturation * hueFrac);
  const f32        tV          = value * (1.0f - saturation * (1.0f - hueFrac));
  switch (hueIndex) {
  case -1: // NOTE: We can get here due to imprecision.
    return geo_color(value, pV, qV, alpha);
  case 0: // Dominant color is red.
    return geo_color(value, tV, pV, alpha);
  case 1: // Dominant color is green.
    return geo_color(qV, value, pV, alpha);
  case 2: // Dominant color is green.
    return geo_color(pV, value, tV, alpha);
  case 3: // Dominant color is blue.
    return geo_color(pV, qV, value, alpha);
  case 4: // Dominant color is blue.
    return geo_color(tV, pV, value, alpha);
  case 5: // Dominant color is red.
    return geo_color(value, pV, qV, alpha);
  case 6: // NOTE: We can get here due to imprecision.
    return geo_color(value, tV, pV, alpha);
  }
  diag_crash_msg("hsv to rgb failed: Invalid hue");
}

void geo_color_pack_f16(const GeoColor color, f16 out[PARAM_ARRAY_SIZE(4)]) {
#if geo_color_simd_enable
  const SimdVec vecF32 = simd_vec_load(color.data);
  SimdVec       vecF16;
  if (g_f16cSupport) {
    // Don't allow the compiler to re-order 'simd_vec_f32_to_f16' before the check.
    COMPILER_BARRIER
    vecF16 = simd_vec_f32_to_f16(vecF32);
  } else {
    vecF16 = simd_vec_f32_to_f16_soft(vecF32);
  }
  const u64 data = simd_vec_u64(vecF16);
  out[0]         = (u16)(data >> 0);
  out[1]         = (u16)(data >> 16);
  out[2]         = (u16)(data >> 32);
  out[3]         = (u16)(data >> 48);
#else
  out[0] = float_f32_to_f16(color.r);
  out[1] = float_f32_to_f16(color.g);
  out[2] = float_f32_to_f16(color.b);
  out[3] = float_f32_to_f16(color.a);
#endif
}
