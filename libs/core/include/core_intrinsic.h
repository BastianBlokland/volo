#pragma once
#include "core.h"

#include <immintrin.h>

#if defined(VOLO_MSVC)

float SYS_DECL  acosf(float);
float SYS_DECL  asinf(float);
float SYS_DECL  atan2f(float, float);
float SYS_DECL  atanf(float);
float SYS_DECL  cosf(float);
double SYS_DECL cos(double);
float SYS_DECL  expf(float);
float SYS_DECL  fmodf(float, float);
double SYS_DECL fmod(double, double);
float SYS_DECL  logf(float);
float SYS_DECL  log10f(float);
float SYS_DECL  powf(float, float);
double SYS_DECL pow(double, double);
float SYS_DECL  sinf(float);
double SYS_DECL sin(double);
double SYS_DECL sqrt(double);
float SYS_DECL  sqrtf(float);
float SYS_DECL  cbrtf(float);
float SYS_DECL  tanf(float);

#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanReverse64)
#pragma intrinsic(acosf)
#pragma intrinsic(asinf)
#pragma intrinsic(atan2f)
#pragma intrinsic(atanf)
#pragma intrinsic(cosf)
#pragma intrinsic(cos)
#pragma intrinsic(expf)
#pragma intrinsic(fmodf)
#pragma intrinsic(fmod)
#pragma intrinsic(logf)
#pragma intrinsic(log10f)
#pragma intrinsic(powf)
#pragma intrinsic(pow)
#pragma intrinsic(sinf)
#pragma intrinsic(sin)
#pragma intrinsic(sqrt)
#pragma intrinsic(sqrtf)
#pragma intrinsic(tanf)

#define intrinsic_acos_f32 acosf
#define intrinsic_asin_f32 asinf
#define intrinsic_atan_f32 atanf
#define intrinsic_atan2_f32 atan2f
#define intrinsic_cos_f32 cosf
#define intrinsic_cos_f64 cos
#define intrinsic_exp_f32 expf
#define intrinsic_fmod_f32 fmodf
#define intrinsic_fmod_f64 fmod
#define intrinsic_log_f32 logf
#define intrinsic_log10_f32 log10f
#define intrinsic_pow_f32 powf
#define intrinsic_pow_f64 pow
#define intrinsic_sin_f32 sinf
#define intrinsic_sin_f64 sin
#define intrinsic_sqrt_f32 sqrtf
#define intrinsic_sqrt_f64 sqrt
#define intrinsic_cbrt_f32 cbrtf
#define intrinsic_tan_f32 tanf

#else

#define intrinsic_acos_f32 __builtin_acosf
#define intrinsic_asin_f32 __builtin_asinf
#define intrinsic_atan_f32 __builtin_atanf
#define intrinsic_atan2_f32 __builtin_atan2f
#define intrinsic_cos_f32 __builtin_cosf
#define intrinsic_cos_f64 __builtin_cos
#define intrinsic_exp_f32 __builtin_expf
#define intrinsic_fmod_f32 __builtin_fmodf
#define intrinsic_fmod_f64 __builtin_fmod
#define intrinsic_log_f32 __builtin_logf
#define intrinsic_log10_f32 __builtin_log10f
#define intrinsic_pow_f32 __builtin_powf
#define intrinsic_pow_f64 __builtin_pow
#define intrinsic_sin_f32 __builtin_sinf
#define intrinsic_sin_f64 __builtin_sin
#define intrinsic_sqrt_f32 __builtin_sqrtf
#define intrinsic_sqrt_f64 __builtin_sqrt
#define intrinsic_cbrt_f32 __builtin_cbrtf
#define intrinsic_tan_f32 __builtin_tanf

#endif

MAYBE_UNUSED INLINE_HINT static f32 intrinsic_round_nearest_f32(const f32 v) {
  return _mm_cvtss_f32(_mm_round_ps(_mm_set1_ps(v), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
}

MAYBE_UNUSED INLINE_HINT static f64 intrinsic_round_nearest_f64(const f64 v) {
  return _mm_cvtsd_f64(_mm_round_pd(_mm_set1_pd(v), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
}

MAYBE_UNUSED INLINE_HINT static f32 intrinsic_round_down_f32(const f32 v) {
  return _mm_cvtss_f32(_mm_round_ps(_mm_set1_ps(v), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
}

MAYBE_UNUSED INLINE_HINT static f64 intrinsic_round_down_f64(const f64 v) {
  return _mm_cvtsd_f64(_mm_round_pd(_mm_set1_pd(v), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
}

MAYBE_UNUSED INLINE_HINT static f32 intrinsic_round_up_f32(const f32 v) {
  return _mm_cvtss_f32(_mm_round_ps(_mm_set1_ps(v), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
}

MAYBE_UNUSED INLINE_HINT static f64 intrinsic_round_up_f64(const f64 v) {
  return _mm_cvtsd_f64(_mm_round_pd(_mm_set1_pd(v), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
}

#define intrinsic_popcnt_32 _mm_popcnt_u32
#define intrinsic_popcnt_64 _mm_popcnt_u64

/**
 * Pre-condition: mask != 0.
 */
MAYBE_UNUSED INLINE_HINT static u8 intrinsic_ctz_32(const u32 mask) {
#if defined(VOLO_MSVC)
  unsigned long res;
  _BitScanForward(&res, mask);
  return res;
#else
  return __builtin_ctz(mask);
#endif
}

/**
 * Pre-condition: mask != 0.
 */
MAYBE_UNUSED INLINE_HINT static u8 intrinsic_ctz_64(const u64 mask) {
#if defined(VOLO_MSVC)
  unsigned long res;
  _BitScanForward64(&res, mask);
  return res;
#else
  return __builtin_ctzll(mask);
#endif
}

/**
 * Pre-condition: mask != 0.
 */
MAYBE_UNUSED INLINE_HINT static u8 intrinsic_clz_32(const u32 mask) {
#if defined(VOLO_MSVC)
  unsigned long res;
  _BitScanReverse(&res, mask);
  return (u8)(31u - res);
#else
  return __builtin_clz(mask);
#endif
}

/**
 * Pre-condition: mask != 0.
 */
MAYBE_UNUSED INLINE_HINT static u8 intrinsic_clz_64(const u64 mask) {
#if defined(VOLO_MSVC)
  unsigned long result;
  _BitScanReverse64(&result, mask);
  return (u8)(63u - result);
#else
  return __builtin_clzll(mask);
#endif
}
