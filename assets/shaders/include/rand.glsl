#ifndef INCLUDE_RAND
#define INCLUDE_RAND

/**
 * Compute a pseudo-random value between 0 (inclusive) and 1 (exclusive) based on the given seed.
 */
f32 rand_f32(const f32v4 seed) {
  const f32 d = dot(seed, f32v4(12.9898, 78.233, 45.164, 94.673));
  return fract(sin(d) * 43758.5453);
}

/**
 * Interleaved Gradient Noise.
 * Source: 'Next Generation Post Processing in Call of Duty: Advanced Warfare'.
 * http://advances.realtimerendering.com/s2014/index.html
 * https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
 */
f32 rand_gradient_noise(const f32v2 seed) {
  const f32v3 magic = f32v3(0.06711056, 0.00583715, 52.9829189);
  return fract(magic.z * fract(dot(seed, magic.xy)));
}

#endif // INCLUDE_RAND
