#ifndef INCLUDE_MATH
#define INCLUDE_MATH

#include "types.glsl"

const f32 c_pi = 3.14159265359;

/**
 * Create a 2x2 matrix that rotates by the given angle in radians.
 */
f32m2 rotate_mat_f32m2(const f32 angle) {
  const f32 s = sin(angle);
  const f32 c = cos(angle);
  return f32m2(c, -s, s, c);
}

#endif // INCLUDE_MATH
