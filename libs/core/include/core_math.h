#pragma once
#include "core_types.h"

/**
 * Return the smallest of the two values.
 */
#define math_min(_A_, _B_) ((_A_) < (_B_) ? (_A_) : (_B_))

/**
 * Return the biggest of the two values.
 */
#define math_max(_A_, _B_) ((_A_) > (_B_) ? (_A_) : (_B_))

/**
 * Returns the sign of the value (-1, 0, or 1).
 */
#define math_sign(_A_) (((_A_) > 0) - ((_A_) < 0))

/**
 * Return the absolute (positive) of the value.
 */
#define math_abs(_A_) ((_A_) < 0 ? -(_A_) : (_A_))

#define math_pi_f32 3.141592653589793238463f
#define math_pi_f64 3.141592653589793238463

/**
 * Raise the given value to the power of 10.
 */
u64 math_pow10_u64(u8);

/**
 * Return the square-root of the given value.
 */
f32 math_sqrt_f32(f32);

/**
 * Return the natural (base e) logarithm of the given value.
 */
f32 math_log_f32(f32);

/**
 * Computes the sine of the given value (in radians).
 */
f32 math_sin_f32(f32);

/**
 * Computes the cosine of the given value (in radians).
 */
f32 math_cos_f32(f32);
