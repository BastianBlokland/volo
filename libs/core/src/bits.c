#include "core_bits.h"
#include "core_diag.h"
#include <immintrin.h>

u32 bits_popcnt(const u32 mask) { return __builtin_popcount(mask); }

u32 bits_ctz(const u32 mask) {
  if (mask == 0u) {
    return 32;
  }
  return __builtin_ctz(mask);
}

u32 bits_clz(const u32 mask) {
  if (mask == 0u) {
    return 32u;
  }
  return __builtin_clz(mask);
}

bool bits_ispow2(const u32 val) {
  diag_assert(val != 0);
  // Ref: https://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2.
  return (val & (val - 1u)) == 0;
}

u32 bits_nextpow2(const u32 val) {
  diag_assert(val != 0u);
  diag_assert(val <= 2147483648u);
  return 1u << (32u - bits_clz(val - 1u));
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
  diag_assert(bits_ispow2(align));

  const u32 rem = val & (align - 1);
  return rem ? align - rem : 0;
}

u32 bits_align(const u32 val, const u32 align) { return val + bits_padding(val, align); }
