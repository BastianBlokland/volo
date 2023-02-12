#ifndef INCLUDE_RAND
#define INCLUDE_RAND

/**
 * Compute a pseudo-random value between 0 (inclusive) and 1 (exclusive) based on the given seed.
 */
f32 rand_f32(const f32v4 seed) {
  const f32 d = dot(seed, f32v4(12.9898, 78.233, 45.164, 94.673));
  return fract(sin(d) * 43758.5453);
}

#endif // INCLUDE_RAND
