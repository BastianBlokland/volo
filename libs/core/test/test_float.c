#include "check_spec.h"
#include "core_bits.h"
#include "core_float.h"

spec(float) {

  it("can detect a NaN float") {
    check(float_isnan(f32_nan));
    check(float_isnan(f64_nan));
    check(float_isnan(bits_u32_as_f32(u32_lit(0xffc00000))));
    check(float_isnan(bits_u64_as_f64(u64_lit(0xfff8000000000000))));
  }

  it("has NaN literals") {
    static const f32 g_nan32 = f32_nan; // Compile time constant.
    check(float_isnan(g_nan32));

    static const f64 g_nan64 = f64_nan; // Compile time constant.
    check(float_isnan(g_nan64));
  }

  it("can detect an infinity float") {
    check(float_isinf(f32_inf));
    check(float_isinf(f64_inf));
    check(float_isinf(bits_u32_as_f32(u32_lit(0x7f800000))));
    check(float_isinf(bits_u64_as_f64(u64_lit(0x7ff0000000000000))));
  }

  it("has infinity literals") {
    static const f32 g_inf32 = f32_inf; // Compile time constant.
    check(float_isinf(g_inf32));

    static const f64 g_inf64 = f64_inf; // Compile time constant.
    check(float_isinf(g_inf64));
  }

  it("has float min literals") {
    check_eq_int(bits_f32_as_u32(f32_min), u32_lit(0xff7fffff));
    check_eq_int(bits_f64_as_u64(f64_min), u64_lit(0xffefffffffffffff));
  }

  it("has float max literals") {
    check_eq_int(bits_f32_as_u32(f32_max), u32_lit(0x7f7fffff));
    check_eq_int(bits_f64_as_u64(f64_max), u64_lit(0x7fefffffffffffff));
  }

  it("can convert between 32 and 16 bit floats") {
    check_eq_float(float_f16_to_f32(float_f32_to_f16(0.0f)), 0.0f, 1e-6f);
    check_eq_float(float_f16_to_f32(float_f32_to_f16(1.0f)), 1.0f, 1e-6f);
    check_eq_float(float_f16_to_f32(float_f32_to_f16(65504.0f)), 65504.0f, 1e-6f);
    check_eq_float(float_f16_to_f32(float_f32_to_f16(6e-5f)), 6e-5f, 1e-6f);
    check_eq_float(float_f16_to_f32(float_f32_to_f16(.42f)), .42f, 1e-3f);
    check_eq_float(float_f16_to_f32(float_f32_to_f16(.1337f)), .1337f, 1e-3f);
    check_eq_float(float_f16_to_f32(float_f32_to_f16(13.37f)), 13.37f, 1e-2f);
    check_eq_float(float_f16_to_f32(float_f32_to_f16(-.42f)), -.42f, 1e-3f);
    check_eq_float(float_f16_to_f32(float_f32_to_f16(-.1337f)), -.1337f, 1e-3f);
    check_eq_float(float_f16_to_f32(float_f32_to_f16(-13.37f)), -13.37f, 1e-2f);
  }

  it("can quantize 32 bit floats to use a limited amount of mantissa bits") {
    check(1.1234f != 1.1235f);
    check(float_quantize_f32(1.1234f, 10) == float_quantize_f32(1.1235f, 10));
  }
}
