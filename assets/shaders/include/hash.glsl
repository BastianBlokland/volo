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

#endif // INCLUDE_HASH
