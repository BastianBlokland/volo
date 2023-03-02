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

f32v2 math_oct_wrap(const f32v2 v) {
  return (1.0 - abs(v.yx)) * f32v2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

/**
 * Encode a normal using Octahedron normal vector encoding.
 * Source: https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
 */
f32v2 math_normal_encode(f32v3 n) {
  n /= abs(n.x) + abs(n.y) + abs(n.z);
  n.xy = n.z >= 0.0 ? n.xy : math_oct_wrap(n.xy);
  n.xy = n.xy * 0.5 + 0.5;
  return n.xy;
}

/**
 * Decode a normal using Octahedron normal vector encoding.
 * Source: https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
 */
f32v3 math_normal_decode(f32v2 f) {
  f           = f * 2.0 - 1.0;
  f32v3     n = f32v3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
  const f32 t = clamp(-n.z, 0, 1);
  n.xy += f32v2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
  return normalize(n);
}

#endif // INCLUDE_MATH
