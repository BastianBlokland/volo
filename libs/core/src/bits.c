#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_intrinsic.h"

static const u32 g_crcPolynomial = 0xEDB88320; // Reversed version of: 0x04C11DB7.
static u32       g_crcTable[256];

static void bits_init_crc(void) {
  /**
   * Compute a CRC32 (ISO 3309) lookup table.
   * Based on the gzip spec:
   * https://www.rfc-editor.org/rfc/rfc1952
   */
  for (u32 i = 0; i != array_elems(g_crcTable); ++i) {
    u32 res = i;
    for (u32 k = 0; k != 8; ++k) {
      if (res & 1) {
        res = g_crcPolynomial ^ (res >> 1);
      } else {
        res >>= 1;
      }
    }
    g_crcTable[i] = res;
  }
}

void bits_init(void) { bits_init_crc(); }

u8 bits_popcnt_32(const u32 mask) { return intrinsic_popcnt_32(mask); }

u8 bits_popcnt_64(const u64 mask) { return intrinsic_popcnt_64(mask); }

u8 bits_ctz_32(const u32 mask) {
  if (mask == 0u) {
    return 32;
  }
  return intrinsic_ctz_32(mask);
}

u8 bits_ctz_64(const u64 mask) {
  if (mask == 0u) {
    return 64;
  }
  return intrinsic_ctz_64(mask);
}

u8 bits_clz_32(const u32 mask) {
  if (mask == 0u) {
    return 32u;
  }
  return intrinsic_clz_32(mask);
}

u8 bits_clz_64(const u64 mask) {
  if (mask == 0u) {
    return 64u;
  }
  return intrinsic_clz_64(mask);
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

u32 bits_nextpow2_32(const u32 val) {
  diag_assert(val != 0u);
  diag_assert(val <= 2147483648u);
  return 1u << (32u - bits_clz_32(val - 1u));
}

u64 bits_nextpow2_64(const u64 val) {
  diag_assert(val != 0u);
  diag_assert(val <= u64_lit(9223372036854775808));
  return u64_lit(1) << (u64_lit(64) - bits_clz_64(val - u64_lit(1)));
}

u32 bits_hash_32(const Mem mem) {
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

  mem_for_u8(mem, itr) {
    hash ^= *itr;
    hash *= prime;
  }

  // Finalize the hash (aka 'mixing').
  hash += hash << 13U;
  hash ^= hash >> 7U;
  hash += hash << 3U;
  hash ^= hash >> 17U;
  hash += hash << 5U;
  return hash;
}

u32 bits_hash_32_val(u32 hash) {
  /**
   * SplitMix32 hash routine.
   * References:
   * - Guy L. Steele, Jr., Doug Lea, and Christine H. Flood. 2014.
   *   Fast splittable pseudorandom number generators.
   */
  hash = (hash ^ (hash >> 16)) * 0x85ebca6b;
  hash = (hash ^ (hash >> 13)) * 0xc2b2ae35;
  return hash ^ (hash >> 16);
}

u64 bits_hash_64_val(u64 hash) {
  /**
   * SplitMix64 hash routine.
   * Reference:
   * - https://xorshift.di.unimi.it/splitmix64.c
   * - http://zimbry.blogspot.com/2011/09/better-bit-mixing-improving-on.html
   */
  hash = (hash ^ (hash >> 30)) * u64_lit(0xbf58476d1ce4e5b9);
  hash = (hash ^ (hash >> 27)) * u64_lit(0x94d049bb133111eb);
  hash = hash ^ (hash >> 31);
  return hash;
}

u32 bits_hash_32_combine(const u32 x, const u32 y) {
  return x ^ (y + 0x9e3779b9 + (x << 6) + (x >> 2));
}

u32 bits_crc_32(const u32 crc, const Mem mem) {
  /**
   * Compute a CRC32 (ISO 3309) checksum with pre and pose conditioning.
   * Based on the gzip spec:
   * https://www.rfc-editor.org/rfc/rfc1952
   */
  u32 res = crc ^ 0xffffffff;
  mem_for_u8(mem, byte) { res = g_crcTable[(res ^ *byte) & 0xff] ^ (res >> 8); }
  return res ^ 0xffffffff;
}

u32 bits_adler_32(u32 adler, const Mem mem) {
  /**
   * Compute the Adler32 checksum of the input data.
   * Based on the zlib spec:
   * https://www.rfc-editor.org/rfc/rfc1950
   */
  enum { Base = 65521 /* Largest prime smaller than 65536 */ };
  u32 s1 = adler & 0xffff;
  u32 s2 = (adler >> 16) & 0xffff;
  mem_for_u8(mem, byte) {
    s1 = (s1 + *byte) % Base;
    s2 = (s2 + s1) % Base;
  }
  return (s2 << 16) + s1;
}

u32 bits_padding_32(const u32 val, const u32 align) {
  diag_assert(align != 0);
  diag_assert(bits_ispow2_32(align));

  return (~val + 1) & (align - 1);
}

u64 bits_padding_64(const u64 val, const u64 align) {
  diag_assert(align != 0);
  diag_assert(bits_ispow2_64(align));

  return (~val + 1) & (align - 1);
}

u32 bits_align_32(const u32 val, const u32 align) { return val + bits_padding_32(val, align); }
u64 bits_align_64(const u64 val, const u64 align) { return val + bits_padding_64(val, align); }

f32 bits_u32_as_f32(const u32 valInput) {
  union {
    u32 valU32;
    f32 valF32;
  } conv;
  conv.valU32 = valInput;
  return conv.valF32;
}

u32 bits_f32_as_u32(const f32 valInput) {
  union {
    u32 valU32;
    f32 valF32;
  } conv;
  conv.valF32 = valInput;
  return conv.valU32;
}

f64 bits_u64_as_f64(const u64 valInput) {
  union {
    u64 valU64;
    f64 valF64;
  } conv;
  conv.valU64 = valInput;
  return conv.valF64;
}

u64 bits_f64_as_u64(const f64 valInput) {
  union {
    u64 valU64;
    f64 valF64;
  } conv;
  conv.valF64 = valInput;
  return conv.valU64;
}
