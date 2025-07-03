#ifndef INCLUDE_RAND
#define INCLUDE_RAND

#include "hash.glsl"
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

/**
 * 1D value noise.
 * Source 'morgan3d' from: https://www.shadertoy.com/view/4dS3Wd
 */
f32 noise_value_f32(const f32 x) {
  const f32 i = floor(x);
  const f32 f = fract(x);
  const f32 u = f * f * (3.0 - 2.0 * f);
  return mix(hash_f32(i), hash_f32(i + 1.0), u);
}

/**
 * 2D value noise.
 * Source 'morgan3d' from: https://www.shadertoy.com/view/4dS3Wd
 */
f32 noise_value_f32v2(const f32v2 x) {
  const f32v2 i = floor(x);
  const f32v2 f = fract(x);

  // Four corners in 2D of a tile
  const f32 a = hash_f32v2(i);
  const f32 b = hash_f32v2(i + f32v2(1.0, 0.0));
  const f32 c = hash_f32v2(i + f32v2(0.0, 1.0));
  const f32 d = hash_f32v2(i + f32v2(1.0, 1.0));

  const f32v2 u = f * f * (3.0 - 2.0 * f);
  return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}


#endif // INCLUDE_RAND
