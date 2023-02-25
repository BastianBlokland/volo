#pragma once
#include "core_types.h"

#define f32_mantissa_bits 23
#define f64_mantissa_bits 52

#define f32_mantissa_max ((((u32)1) << f32_mantissa_bits) - 1)
#define f64_mantissa_max ((((u64)1) << f64_mantissa_bits) - 1)

#define f32_exponent_max 38
#define f64_exponent_max 308

#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define f32_nan (__builtin_nanf(""))
#else
#define f32_nan (0.0f / 0.0f)
#endif

#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define f64_nan (__builtin_nan(""))
#else
#define f64_nan (0.0 / 0.0)
#endif

#define f32_inf (1.0f / 0.0f)
#define f64_inf (1.0 / 0.0)

#define f32_max 3.402823466e+38f
#define f64_max 1.7976931348623158e+308

#define f32_min -f32_max
#define f64_min -f64_max

#define f32_epsilon 1e-7f
#define f64_epsilon 1e-16

/**
 * Returns true if the given floating point number is 'Not A Number'.
 * NOTE: _VAL_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
#define float_isnan(_VAL_) (__builtin_isnan(_VAL_))
#else
#define float_isnan(_VAL_) ((_VAL_) != (_VAL_))
#endif

/**
 * Returns true if the given floating point number is equal to infinity.
 * NOTE: _VAL_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define float_isinf(_VAL_) ((_VAL_) != 0.0 && (_VAL_)*2 == (_VAL_))

/**
 * Convert a 32 bit floating point value to 16 bit.
 */
f16 float_f32_to_f16(f32);

/**
 * Convert a 16 bit floating point value to 32 bit.
 */
f32 float_f16_to_f32(f16);

/**
 * Quantize a float to use a limited number of mantissa bits.
 * Pre-condition: maxMantissaBits > 0 && maxMantissaBits <= 23
 */
f32 float_quantize_f32(f32, u8 maxMantissaBits);
