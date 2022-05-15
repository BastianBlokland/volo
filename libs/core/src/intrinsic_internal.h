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
#pragma intrinsic(floor)
#pragma intrinsic(floorf)
#pragma intrinsic(fmodf)
#pragma intrinsic(logf)
#pragma intrinsic(powf)
#pragma intrinsic(sinf)
#pragma intrinsic(sqrt)
#pragma intrinsic(sqrtf)
#pragma intrinsic(tanf)

#define intrinsic_acosf acosf
#define intrinsic_asinf asinf
#define intrinsic_atan2f atan2f
#define intrinsic_atanf atanf
#define intrinsic_ceil ceil
#define intrinsic_ceilf ceilf
#define intrinsic_cosf cosf
#define intrinsic_floor floor
#define intrinsic_floorf floorf
#define intrinsic_fmodf fmodf
#define intrinsic_logf logf
#define intrinsic_powf powf
#define intrinsic_round round
#define intrinsic_roundf roundf
#define intrinsic_sinf sinf
#define intrinsic_sqrt sqrt
#define intrinsic_sqrtf sqrtf
#define intrinsic_tanf tanf

#else

#define intrinsic_acosf __builtin_acosf
#define intrinsic_asinf __builtin_asinf
#define intrinsic_atan2f __builtin_atan2f
#define intrinsic_atanf __builtin_atanf
#define intrinsic_ceil __builtin_ceil
#define intrinsic_ceilf __builtin_ceilf
#define intrinsic_cosf __builtin_cosf
#define intrinsic_floor __builtin_floor
#define intrinsic_floorf __builtin_floorf
#define intrinsic_fmodf __builtin_fmodf
#define intrinsic_logf __builtin_logf
#define intrinsic_powf __builtin_powf
#define intrinsic_round __builtin_round
#define intrinsic_roundf __builtin_roundf
#define intrinsic_sinf __builtin_sinf
#define intrinsic_sqrt __builtin_sqrt
#define intrinsic_sqrtf __builtin_sqrtf
#define intrinsic_tanf __builtin_tanf

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
