#ifndef INCLUDE_HASH
#define INCLUDE_HASH

#include "types.glsl"

/**
 * Compute a hash for the given unsigned 32 bit value.
 * bias: 0.17353355999581582 (very probably the best of its kind).
 * Source 'lowbias32' from: https://www.shadertoy.com/view/WttXWX
 */
u32 hash_u32(u32 x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

// Precision-adjusted variations of https://www.shadertoy.com/view/4djSRW
f32 hash_f32(f32 p) {
  p = fract(p * 0.011);
  p *= p + 7.5;
  p *= p + p;
  return fract(p);
}

f32 hash_f32v2(f32v2 p) {
  f32v3 p3 = fract(f32v3(p.xyx) * 0.13);
  p3 += dot(p3, p3.yzx + 3.333);
  return fract((p3.x + p3.y) * p3.z);
}

#endif // INCLUDE_HASH
