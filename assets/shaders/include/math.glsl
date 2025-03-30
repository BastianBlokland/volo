#ifndef INCLUDE_MATH
#define INCLUDE_MATH

#include "types.glsl"

const f32 c_pi = 3.14159265359;

/**
 * Create a 2x2 matrix that rotates by the given angle in radians.
 */
f32m2 math_rotate_f32m2(const f32 angle) {
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
 *
 * The hemispheres are oriented along the positive y axis. The space is continuous in the same
 * hemisphere but not across hemispheres, this means linear blending can be used as long as the
 * source and destination points are both on the same hemisphere.
 */
f32v2 math_normal_encode(f32v3 n) {
  n /= abs(n.x) + abs(n.y) + abs(n.z);
  return (n.y >= 0.0 ? n.xz : math_oct_wrap(n.xz)) * 0.5 + 0.5;
}

/**
 * Decode a normal using Octahedron normal vector encoding.
 * Source: https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
 */
f32v3 math_normal_decode(f32v2 f) {
  f           = f * 2.0 - 1.0;
  f32v3     n = f32v3(f.x, 1.0 - abs(f.x) - abs(f.y), f.y);
  const f32 t = clamp(-n.y, 0, 1);
  n.xz += f32v2(n.x >= 0.0 ? -t : t, n.z >= 0.0 ? -t : t);
  return normalize(n);
}

/**
 * Find the closest position on the line formed by two points to the given point.
 */
f32v3 math_line_closest_point(const f32v3 lineA, const f32v3 lineB, const f32v3 point) {
  const f32v3 toB       = lineB - lineA;
  const f32   lengthSqr = dot(toB, toB);
  if (lengthSqr == 0.0) {
    return lineA; // Zero length line.
  }
  const f32 t = dot(point - lineA, toB) / lengthSqr;
  return lineA + toB * clamp(t, 0.0, 1.0);
}

#endif // INCLUDE_MATH
