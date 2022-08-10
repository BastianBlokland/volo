#pragma once
#include <immintrin.h>

#if defined(VOLO_MSVC)
#include <intrin.h>
#include <math.h>
#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanReverse64)
#pragma intrinsic(acosf)
#pragma intrinsic(asinf)
#pragma intrinsic(atan2f)
#pragma intrinsic(atanf)
#pragma intrinsic(ceil)
#pragma intrinsic(ceilf)
#pragma intrinsic(cosf)
#pragma intrinsic(expf)
#pragma intrinsic(floor)
#pragma intrinsic(floorf)
#pragma intrinsic(fmodf)
#pragma intrinsic(logf)
#pragma intrinsic(powf)
#pragma intrinsic(sinf)
#pragma intrinsic(sqrt)
#pragma intrinsic(sqrtf)
#pragma intrinsic(tanf)

#define intrinsic_acos_f32 acosf
#define intrinsic_asin_f32 asinf
#define intrinsic_atan_f32 atanf
#define intrinsic_atan2_f32 atan2f
#define intrinsic_ceil_f32 ceilf
#define intrinsic_ceil_f64 ceil
#define intrinsic_cos_f32 cosf
#define intrinsic_exp_f32 expf
#define intrinsic_floor_f32 floorf
#define intrinsic_floor_f64 floor
#define intrinsic_fmod_f32 fmodf
#define intrinsic_log_f32 logf
#define intrinsic_pow_f32 powf
#define intrinsic_round_f32 roundf
#define intrinsic_round_f64 round
#define intrinsic_sin_f32 sinf
#define intrinsic_sqrt_f32 sqrtf
#define intrinsic_sqrt_f64 sqrt
#define intrinsic_tan_f32 tanf

#else

#define intrinsic_acos_f32 __builtin_acosf
#define intrinsic_asin_f32 __builtin_asinf
#define intrinsic_atan_f32 __builtin_atanf
#define intrinsic_atan2_f32 __builtin_atan2f
#define intrinsic_ceil_f32 __builtin_ceilf
#define intrinsic_ceil_f64 __builtin_ceil
#define intrinsic_cos_f32 __builtin_cosf
#define intrinsic_exp_f32 __builtin_expf
#define intrinsic_floor_f32 __builtin_floorf
#define intrinsic_floor_f64 __builtin_floor
#define intrinsic_fmod_f32 __builtin_fmodf
#define intrinsic_log_f32 __builtin_logf
#define intrinsic_pow_f32 __builtin_powf
#define intrinsic_round_f32 __builtin_roundf
#define intrinsic_round_f64 __builtin_round
#define intrinsic_sin_f32 __builtin_sinf
#define intrinsic_sqrt_f32 __builtin_sqrtf
#define intrinsic_sqrt_f64 __builtin_sqrt
#define intrinsic_tan_f32 __builtin_tanf

#endif

#define intrinsic_popcnt_32 _mm_popcnt_u32
#define intrinsic_popcnt_64 _mm_popcnt_u64

/**
 * Pre-condition: mask != 0.
 */
INLINE_HINT static u8 intrinsic_ctz_32(const u32 mask) {
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
INLINE_HINT static u8 intrinsic_ctz_64(const u64 mask) {
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
INLINE_HINT static u8 intrinsic_clz_32(const u32 mask) {
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
INLINE_HINT static u8 intrinsic_clz_64(const u64 mask) {
#if defined(VOLO_MSVC)
  unsigned long result;
  _BitScanReverse64(&result, mask);
  return (u8)(63u - result);
#else
  return __builtin_clzll(mask);
#endif
}
