#ifndef INCLUDE_HASH
#define INCLUDE_HASH

#include "types.glsl"

/**
 * Compute number between 0 (inclusive) and 1 (exclusive) for the given value.
 * Source 'Dave_Hoskins' from: https://www.shadertoy.com/view/4djSRW
 */
f32 hash_f32(f32 x) {
  x = fract(x * 0.011);
  x *= x + 7.5;
  x *= x + x;
  return fract(x);
}

/**
 * Compute number between 0 (inclusive) and 1 (exclusive) for the given value.
 * Source 'Dave_Hoskins' from: https://www.shadertoy.com/view/4djSRW
 */
f32 hash_f32v2(f32v2 x) {
  f32v3 x3 = fract(f32v3(x.xyx) * 0.13);
  x3 += dot(x3, x3.yzx + 3.333);
  return fract((x3.x + x3.y) * x3.z);
}

/**
 * Compute number between 0 (inclusive) and 1 (exclusive) for the given value.
 * Source 'Dave_Hoskins' from: https://www.shadertoy.com/view/4djSRW
 */
f32 hash_f32v3(f32v3 x) {
  x  = fract(x * 0.1031);
  x += dot(x, x.zyx + 31.32);
  return fract((x.x + x.y) * x.z);
}

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

#endif // INCLUDE_HASH
