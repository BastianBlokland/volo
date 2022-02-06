#include "core_bits.h"
#include "core_float.h"

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
