#ifndef INCLUDE_RAND
#define INCLUDE_RAND

#include "types.glsl"

/**
 * Interleaved Gradient Noise.
 * Source: 'Next Generation Post Processing in Call of Duty: Advanced Warfare'.
 * http://advances.realtimerendering.com/s2014/index.html
 * https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
 */
f32 noise_gradient_f32v2(const f32v2 seed) {
  const f32v3 magic = f32v3(0.06711056, 0.00583715, 52.9829189);
  return fract(magic.z * fract(dot(seed, magic.xy)));
}

#endif // INCLUDE_RAND
