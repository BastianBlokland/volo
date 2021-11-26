#pragma once
#include "core_types.h"

/**
 * Return the smallest of the two values.
 * NOTE: _A_ and _B_ are expanded multiple times, so care must be taken when providing complex
 * expressions.
 */
#define math_min(_A_, _B_) ((_A_) < (_B_) ? (_A_) : (_B_))

/**
 * Return the biggest of the two values.
 * NOTE: _A_ and _B_ are expanded multiple times, so care must be taken when providing complex
 * expressions.
 */
#define math_max(_A_, _B_) ((_A_) > (_B_) ? (_A_) : (_B_))

/**
 * Returns the sign of the value (-1, 0, or 1).
 * NOTE: _A_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define math_sign(_A_) (((_A_) > 0) - ((_A_) < 0))

/**
 * Return the absolute (positive) of the value.
 * NOTE: _A_ is expanded multiple times, so care must be taken when providing complex expressions.
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
 * Computes the arc (inverse) sine of the given value (in radians).
 */
f32 math_asin_f32(f32);

/**
 * Computes the cosine of the given value (in radians).
 */
f32 math_cos_f32(f32);

/**
 * Computes the arc (inverse) cosine of the given value (in radians).
 */
f32 math_acos_f32(f32);

/**
 * Compute the integer part of the given value (removes the fractional part).
 */
f64 math_trunc_f64(f64);

/**
 * Compute the floor (round-down) of the given value.
 */
f64 math_floor_f64(f64);

/**
 * Compute the ceiling (round-up) of the given value.
 */
f64 math_ceil_f64(f64);

/**
 * Compute the rounded version of the given value.
 * NOTE: Uses round-to-even for values exactly half-way between two values (also known as 'Bankers
 * rounding').
 */
f64 math_round_f64(f64);

/**
 * Clamp the given value between min (inclusive) and max (inclusive).
 */
f32 math_clamp_f32(f32 val, f32 min, f32 max);
