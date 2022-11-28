#include "core_bits.h"
#include "core_float.h"

#include <immintrin.h>

#if !defined(VOLO_MSVC)
#include <cpuid.h>
#endif

static f16 (*g_floatF32ToF16Impl)(f32);
static f32 (*g_floatF16ToF32Impl)(f16);

static void float_cpu_id(const i32 functionId, i32 output[4]) {
#if defined(VOLO_MSVC)
  __cpuid(output, functionId);
#else
  __cpuid(functionId, output[0], output[1], output[2], output[3]);
#endif
}

/**
 * Check if the cpu supports the f16c (16 bit float conversions) instructions.
 */
static bool float_cpu_f16c_support() {
  /**
   * Check the f16c cpu feature flag.
   * More info: https://en.wikipedia.org/wiki/CPUID#EAX=1:_Processor_Info_and_Feature_Bits
   */
  i32 cpuId[4];
  float_cpu_id(1, cpuId);
  return (cpuId[2] & (1 << 29)) != 0;
}

static f16 float_f32_to_f16_intrinsic(const f32 val) {
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

static f16 float_f32_to_f16_soft(const f32 val) {
  /**
   * IEEE-754 16-bit floating-point format (without infinity):
   * 1-5-10, exp-15, +-131008.0, +-6.1035156E-5, +-5.9604645E-8, 3.311 digits
   *
   * Source: Answer by user 'ProjectPhysX' on the following StackOverflow question:
   * https://stackoverflow.com/questions/1659440/32-bit-to-16-bit-floating-point-conversion
   */

  // Round-to-nearest-even: add last bit after truncated mantissa
  const u32 b = bits_f32_as_u32(val) + 0x00001000;
  const u32 e = (b & 0x7F800000) >> 23; // exponent
  const u32 m = b & 0x007FFFFF; // Mantissa; in line below: 0x007FF000 = 0x00800000-0x00001000
                                // = decimal indicator flag - initial rounding

  /**
   * TODO: The following code contains UB as some of the intermediate values can be shifted more
   * then their type allows. The resulting (overflowed) value is not actually used in that case but
   * strictly speaking it is UB.
   */
  return (b & 0x80000000) >> 16 | (e > 112) * ((((e - 112) << 10) & 0x7C00) | m >> 13) |
         ((e < 113) & (e > 101)) * ((((0x007FF000 + m) >> (125 - e)) + 1) >> 1) |
         (e > 143) * 0x7FFF; // Sign : normalized : denormalized : saturate
}

static f32 float_f16_to_f32_intrinsic(const f16 val) {
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

static f32 float_f16_to_f32_soft(const f16 val) {
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

  /**
   * TODO: The following code contains UB as some of the intermediate values can be shifted more
   * then their type allows. The resulting (overflowed) value is not actually used in that case but
   * strictly speaking it is UB.
   */
  return bits_u32_as_f32(
      (val & 0x8000) << 16 | (e != 0) * ((e + 112) << 23 | m) |
      ((e == 0) & (m != 0)) *
          ((v - 37) << 23 | ((m << (150 - v)) & 0x007FE000))); // Sign : normalized : denormalized
}

void float_init() {
  g_floatF32ToF16Impl =
      float_cpu_f16c_support() ? float_f32_to_f16_intrinsic : float_f32_to_f16_soft;
  g_floatF16ToF32Impl =
      float_cpu_f16c_support() ? float_f16_to_f32_intrinsic : float_f16_to_f32_soft;
}

f16 float_f32_to_f16(const f32 val) { return g_floatF32ToF16Impl(val); }
f32 float_f16_to_f32(const f16 val) { return g_floatF16ToF32Impl(val); }

f32 float_quantize_f32(const f32 val, const u8 maxMantissaBits) {
  u32 valBits = bits_f32_as_u32(val);

  /**
   * Generates +-inf for overflow, preserves NaN, flushes denormals to zero, rounds to nearest.
   * Based on the MeshOptimizer implementation by Zeux (https://github.com/zeux/meshoptimizer).
   */

  const i32 mask           = (1 << (f32_mantissa_bits - maxMantissaBits)) - 1;
  const i32 round          = (1 << (f32_mantissa_bits - maxMantissaBits)) >> 1;
  const i32 exp            = valBits & 0x7f800000;
  const u32 roundedValBits = (valBits + round) & ~mask;

  // Round all numbers except inf/nan; this is important to make sure nan doesn't overflow into -0.
  valBits = exp == 0x7f800000 ? valBits : roundedValBits;

  // Flush denormals to zero.
  valBits = exp == 0 ? 0 : valBits;

  return bits_u32_as_f32(valBits);
}
