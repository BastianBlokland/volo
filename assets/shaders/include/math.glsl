#ifndef INCLUDE_MATH
#define INCLUDE_MATH

#include "types.glsl"

const f32 c_pi = 3.14159265359;

/**
 * Rotate the vector by the given angle in radians.
 */
f32v2 rotate_f32v2(const f32v2 v, const f32 angle) {
  const f32   s   = sin(angle);
  const f32   c   = cos(angle);
  const f32m2 rot = f32m2(c, -s, s, c);
  return rot * v;
}

#endif // INCLUDE_MATH
