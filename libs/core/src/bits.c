#include "core_bits.h"
#include "core_diag.h"

#include <immintrin.h>

#if defined(VOLO_MSVC)
#include "intrin.h"
#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)
#else // !VOLO_MSVC
#include <cpuid.h>
#endif

static f16 (*g_bitsF32ToF16Impl)(f32);
static f32 (*g_bitsF16ToF32Impl)(f16);

static void bits_cpu_id(const i32 functionId, i32 output[4]) {
#if defined(VOLO_MSVC)
  __cpuid(output, functionId);
#else
  __cpuid(functionId, output[0], output[1], output[2], output[3]);
#endif
}

/**
 * Check if the cpu supports the f16c (16 bit float conversions) instructions.
 */
static bool bits_cpu_f16c_support() {
  /**
   * Check the f16c cpu feature flag.
   * More info: https://en.wikipedia.org/wiki/CPUID#EAX=1:_Processor_Info_and_Feature_Bits
   */
  i32 cpuId[4];
  bits_cpu_id(1, cpuId);
  return (cpuId[2] & (1 << 29)) != 0;
}

static f16 bits_f32_to_f16_intrinsic(const f32 val) {
/**
 * Intel intrinsic for converting float to half.
 * https://software.intel.com/sites/landingpage/IntrinsicsGuide/#text=_cvtss_sh
 */
#if defined(VOLO_MSVC)
  // MSVC doesn't define the single value '_cvtss_sh'.
  return _mm_cvtsi128_si32(_mm_cvtps_ph(_mm_set_ss(val), _MM_FROUND_TO_NEAREST_INT));
#else
  return _cvtss_sh(val, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
#endif
}

static f16 bits_f32_to_f16_soft(const f32 val) {
  /**
   * IEEE-754 16-bit floating-point format (without infinity):
   * 1-5-10, exp-15, +-131008.0, +-6.1035156E-5, +-5.9604645E-8, 3.311 digits
   *
   * Source: Awnser of user 'ProjectPhysX' on the following StackOverflow question:
   * https://stackoverflow.com/questions/1659440/32-bit-to-16-bit-floating-point-conversion
   */

  // Round-to-nearest-even: add last bit after truncated mantissa
  const u32 b = bits_f32_as_u32(val) + 0x00001000;
  const u32 e = (b & 0x7F800000) >> 23; // exponent
  const u32 m = b & 0x007FFFFF; // Mantissa; in line below: 0x007FF000 = 0x00800000-0x00001000
                                // = decimal indicator flag - initial rounding
  return (b & 0x80000000) >> 16 | (e > 112) * ((((e - 112) << 10) & 0x7C00) | m >> 13) |
         ((e < 113) & (e > 101)) * ((((0x007FF000 + m) >> (125 - e)) + 1) >> 1) |
         (e > 143) * 0x7FFF; // Sign : normalized : denormalized : saturate
}

static f32 bits_f16_to_f32_intrinsic(const f16 val) {
  /**
   * Intel intrinsic for converting half to float.
   * https://software.intel.com/sites/landingpage/IntrinsicsGuide/#text=_cvtsh_ss
   */
#if defined(VOLO_MSVC)
  // MSVC doesn't define the single value '_cvtsh_ss'.
  return _mm_cvtss_f32(_mm_cvtph_ps(_mm_cvtsi32_si128(val)));
#else
  return _cvtsh_ss(val);
#endif
}

static f32 bits_f16_to_f32_soft(const f16 val) {
  /**
   * IEEE-754 16-bit floating-point format (without infinity):
   * 1-5-10, exp-15, +-131008.0, +-6.1035156E-5, +-5.9604645E-8, 3.311 digits
   *
   * Source: Awnser of user 'ProjectPhysX' on the following StackOverflow question:
   * https://stackoverflow.com/questions/1659440/32-bit-to-16-bit-floating-point-conversion
   */

  const u32 e = (val & 0x7C00) >> 10; // Exponent
  const u32 m = (val & 0x03FF) << 13; // Mantissa

  // Evil log2 bit hack to count leading zeros in denormalized format:
  const u32 v = bits_f32_as_u32((f32)m) >> 23;

  return bits_u32_as_f32(
      (val & 0x8000) << 16 | (e != 0) * ((e + 112) << 23 | m) |
      ((e == 0) & (m != 0)) *
          ((v - 37) << 23 | ((m << (150 - v)) & 0x007FE000))); // Sign : normalized : denormalized
}

void bits_init() {
  g_bitsF32ToF16Impl = bits_cpu_f16c_support() ? bits_f32_to_f16_intrinsic : bits_f32_to_f16_soft;
  g_bitsF16ToF32Impl = bits_cpu_f16c_support() ? bits_f16_to_f32_intrinsic : bits_f16_to_f32_soft;
}

u8 bits_popcnt_32(const u32 mask) {
#if defined(VOLO_MSVC)
  return __popcnt(mask);
#else
  return __builtin_popcount(mask);
#endif
}

u8 bits_popcnt_64(const u64 mask) {
#if defined(VOLO_MSVC)
  return __popcnt64(mask);
#else
  return __builtin_popcountll(mask);
#endif
}

u8 bits_ctz_32(const u32 mask) {
  if (mask == 0u) {
    return 32;
  }
#if defined(VOLO_MSVC)
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
#if defined(VOLO_MSVC)
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
#if defined(VOLO_MSVC)
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
#if defined(VOLO_MSVC)
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

u32 bits_padding_32(const u32 val, const u32 align) {
  diag_assert(align != 0);
  diag_assert(bits_ispow2_32(align));

  const u32 rem = val & (align - 1);
  return rem ? align - rem : 0;
}

u64 bits_padding_64(const u64 val, const u64 align) {
  diag_assert(align != 0);
  diag_assert(bits_ispow2_64(align));

  const u64 rem = val & (align - 1);
  return rem ? align - rem : 0;
}

u32 bits_align_32(const u32 val, const u32 align) { return val + bits_padding_32(val, align); }
u64 bits_align_64(const u64 val, const u64 align) { return val + bits_padding_64(val, align); }

f32 bits_u32_as_f32(u32 val) { return *(f32*)(&val); }
u32 bits_f32_as_u32(f32 val) { return *(u32*)(&val); }
f64 bits_u64_as_f64(u64 val) { return *(f64*)(&val); }
u64 bits_f64_as_u64(f64 val) { return *(u64*)(&val); }

f16 bits_f32_to_f16(const f32 val) { return g_bitsF32ToF16Impl(val); }
f32 bits_f16_to_f32(const f16 val) { return g_bitsF16ToF32Impl(val); }
