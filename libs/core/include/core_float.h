#pragma once
#include "core_types.h"

#define f32_mantissa_bits 23
#define f64_mantissa_bits 52

#define f32_mantissa_max ((((u32)1) << f32_mantissa_bits) - 1)
#define f64_mantissa_max ((((u64)1) << f64_mantissa_bits) - 1)

#define f32_exponent_max 38
#define f64_exponent_max 308

#define f32_nan (0.0f / 0.0f)
#define f64_nan (0.0 / 0.0)

#define f32_inf (1.0f / 0.0f)
#define f64_inf (1.0 / 0.0)

#define f32_max 3.402823466e+38f
#define f64_max 1.7976931348623158e+308

#define f32_min -f32_max
#define f64_min -f64_max

#define f32_epsilon 1.401298E-45f
#define f64_epsilon 4.94065645841247E-324

/**
 * Returns true if the given floating point number is 'Not A Number'.
 * NOTE: _VAL_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define float_isnan(_VAL_) ((_VAL_) != (_VAL_))

/**
 * Returns true if the given floating point number is equal to infinity.
 * NOTE: _VAL_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define float_isinf(_VAL_) ((_VAL_) != 0.0 && (_VAL_)*2 == (_VAL_))
