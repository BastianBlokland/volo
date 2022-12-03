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

/**
 * Compute the linearly interpolated value from x to y at time t.
 * NOTE: Does not clamp t (so can extrapolate too).
 * NOTE: _X_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define math_lerp(_X_, _Y_, _T_) ((_X_) + ((_Y_) - (_X_)) * (_T_))

/**
 * Opposite of lerp, calculate at what t the value lies in respect to x and y.
 * NOTE: does not clamp the value (so can return less then 0 or more then 1).
 * NOTE: _X_ is expanded multiple times, so care must be taken when providing complex expressions.
 * NOTE: _Y_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define math_unlerp(_X_, _Y_, _VALUE_)                                                             \
  (((_X_) == (_Y_)) ? 0 : ((_VALUE_) - (_X_)) / ((_Y_) - (_X_)))

#define math_pi_f32 3.141592653589793238463f
#define math_pi_f64 3.141592653589793238463
#define math_deg_to_rad 0.0174532924f
#define math_rad_to_deg 57.29578f

/**
 * Raise the given value to the power of 10.
 */
u64 math_pow10_u64(u8);

/**
 * Computes the remainder of dividing x by y.
 */
f32 math_mod_f32(f32 x, f32 y);

/**
 * Return the square-root of the given value.
 */
f32 math_sqrt_f32(f32);
f64 math_sqrt_f64(f64);

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
 * Compute the tangent of the given value (in radians).
 */
f32 math_tan_f32(f32);

/**
 * Compute the arc (inverse) tangent of the given value (in radians).
 */
f32 math_atan_f32(f32);

/**
 * Compute the arc (inverse) tangent of the given value (in radians) in two dimensions.
 * Represents the angle from the x-axis to a line containing the origin and a point at x,y.
 */
f32 math_atan2_f32(f32 x, f32 y);

/**
 * Compute the given base to the power of exp.
 */
f32 math_pow_f32(f32 base, f32 exp);

/**
 * Compute the natural logarithm e raised to the power of exp.
 */
f32 math_exp_f32(f32 exp);

/**
 * Compute the integer part of the given value (removes the fractional part).
 */
f32 math_trunc_f32(f32);
f64 math_trunc_f64(f64);

/**
 * Round the given value to an integer value.
 */
f32 math_round_nearest_f32(f32);
f64 math_round_nearest_f64(f64);
f32 math_round_down_f32(f32);
f64 math_round_down_f64(f64);
f32 math_round_up_f32(f32);
f64 math_round_up_f64(f64);

/**
 * Clamp the given value between min (inclusive) and max (inclusive).
 */
f32 math_clamp_f32(f32 val, f32 min, f32 max);
f64 math_clamp_f64(f64 val, f64 min, f64 max);
i32 math_clamp_i32(i32 val, i32 min, i32 max);
i64 math_clamp_i64(i64 val, i64 min, i64 max);

/**
 * Moves the given value towards the target with a maximum step-size of maxDelta.
 * Returns true if we've reached the target.
 */
bool math_towards_f32(f32* val, f32 target, f32 maxDelta);
