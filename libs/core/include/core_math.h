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
 * Raise the given value to the power of 10.
 */
u64 math_pow10_u64(u8);
