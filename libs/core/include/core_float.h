#pragma once
#include "core_bits.h"
#include "core_types.h"

#define f32_mantissa_bits 23
#define f64_mantissa_bits 52

#define f32_mantissa_max ((((u32)1) << f32_mantissa_bits) - 1)
#define f64_mantissa_max ((((u64)1) << f64_mantissa_bits) - 1)

#define f32_exponent_max 38
#define f64_exponent_max 308

#define f32_nan bits_u32_as_f32(0x7fc00000u)
#define f64_nan bits_u64_as_f64(0x7ff8000000000000ull)

#define f32_inf bits_u32_as_f32(0x7f800000u)
#define f64_inf bits_u64_as_f64(0x7ff0000000000000ull)

#define f32_min bits_u32_as_f32(0xff7fffffu)
#define f64_min bits_u64_as_f64(0xffefffffffffffffull)

#define f32_max bits_u32_as_f32(0x7f7fffffu)
#define f64_max bits_u64_as_f64(0x7fefffffffffffffull)

#define f32_epsilon 1.401298E-45
#define f64_epsilon 4.94065645841247E-324

/**
 * Returns true if the given floating point number is 'Not A Number'.
 */
#define float_isnan(_VAL_) ((_VAL_) != (_VAL_))

/**
 * Returns true if the given floating point number is equal to infinity.
 */
#define float_isinf(_VAL_) ((_VAL_) != 0.0 && (_VAL_)*2 == (_VAL_))
