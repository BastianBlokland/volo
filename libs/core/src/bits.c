#include "core_bits.h"
#include "core_diag.h"
#include <immintrin.h>

#ifdef VOLO_MSVC
#include "intrin.h"
#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)
#endif

u8 bits_popcnt_32(const u32 mask) {
#ifdef VOLO_MSVC
  return __popcnt(mask);
#else
  return __builtin_popcount(mask);
#endif
}

u8 bits_popcnt_64(const u64 mask) {
#ifdef VOLO_MSVC
  return __popcnt64(mask);
#else
  return __builtin_popcountll(mask);
#endif
}

u8 bits_ctz_32(const u32 mask) {
  if (mask == 0u) {
    return 32;
  }
#ifdef VOLO_MSVC
  unsigned long result;
  _BitScanForward(&result, mask);
  return (u8)result;
#else
  return __builtin_ctz(mask);
#endif
}

u8 bits_ctz_64(const u64 mask) {
  if (mask == 0u) {
    return 64;
  }
#ifdef VOLO_MSVC
  unsigned long result;
  _BitScanForward64(&result, mask);
  return (u8)result;
#else
  return __builtin_ctzll(mask);
#endif
}

u8 bits_clz_32(const u32 mask) {
  if (mask == 0u) {
    return 32u;
  }
#ifdef VOLO_MSVC
  unsigned long result;
  _BitScanReverse(&result, mask);
  return (u32)(31u - result);
#else
  return __builtin_clz(mask);
#endif
}

u8 bits_clz_64(const u64 mask) {
  if (mask == 0u) {
    return 64u;
  }
#ifdef VOLO_MSVC
  unsigned long result;
  _BitScanReverse64(&result, mask);
  return (u8)(63u - result);
#else
  return __builtin_clzll(mask);
#endif
}

bool bits_ispow2_32(const u32 val) {
  diag_assert(val != 0);
  // Ref: https://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2.
  return (val & (val - 1u)) == 0;
}

bool bits_ispow2_64(const u64 val) {
  diag_assert(val != 0);
  // Ref: https://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2.
  return (val & (val - 1u)) == 0;
}

u32 bits_nextpow2(const u32 val) {
  diag_assert(val != 0u);
  diag_assert(val <= 2147483648u);
  return 1u << (32u - bits_clz_32(val - 1u));
}

u32 bits_hash32(const Mem mem) {
  /**
   * Fowler–Noll–Vo hash function.
   * Ref: http://www.isthe.com/chongo/tech/comp/fnv/index.html
   * Ref: https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
   *
   * FNV-1a.
   * 32-bit
   * prime: 2^24 + 2^8 + 0x93 = 16777619
   * offset: 2166136261
   */

  const u32 prime = 16777619u;
  u32       hash  = 2166136261U;

  mem_for_u8(mem, byte, {
    hash ^= byte;
    hash *= prime;
  });

  // Finalize the hash (aka 'mixing').
  hash += hash << 13U;
  hash ^= hash >> 7U;
  hash += hash << 3U;
  hash ^= hash >> 17U;
  hash += hash << 5U;
  return hash;
}

u32 bits_padding(const u32 val, const u32 align) {
  diag_assert(align != 0);
  diag_assert(bits_ispow2_32(align));

  const u32 rem = val & (align - 1);
  return rem ? align - rem : 0;
}

u32 bits_align(const u32 val, const u32 align) { return val + bits_padding(val, align); }

f32 bits_u32_as_f32(u32 val) { return *(f32*)(&val); }
u32 bits_f32_as_u32(f32 val) { return *(u32*)(&val); }
f64 bits_u64_as_f64(u64 val) { return *(f64*)(&val); }
u64 bits_f64_as_u64(f64 val) { return *(u64*)(&val); }
