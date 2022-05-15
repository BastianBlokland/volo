#pragma once
#include <immintrin.h>

#if defined(VOLO_MSVC)
#include <intrin.h>
#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse64)
#endif

#define intrinsic_popcnt_32(_MASK_) _mm_popcnt_u32(_MASK_)
#define intrinsic_popcnt_64(_MASK_) _mm_popcnt_u64(_MASK_)

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
  return __builtin_clz(mask);
#endif
}
