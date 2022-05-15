#pragma once
#include <immintrin.h>

// clang-format off

#define intrinsic_popcnt_64 _mm_popcnt_u64

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
