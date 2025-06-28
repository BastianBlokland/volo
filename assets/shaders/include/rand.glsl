#ifndef INCLUDE_RAND
#define INCLUDE_RAND

#include "types.glsl"
#include "hash.glsl"

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

f32 rand_hash_noise(vec2 x) {
    vec2 i = floor(x);
    vec2 f = fract(x);

  // Four corners in 2D of a tile
  f32 a = hash_f32v2(i);
    f32 b = hash_f32v2(i + vec2(1.0, 0.0));
    f32 c = hash_f32v2(i + vec2(0.0, 1.0));
    f32 d = hash_f32v2(i + vec2(1.0, 1.0));

  // Simple 2D lerp using smoothstep envelope between the values.
  // return vec3(mix(mix(a, b, smoothstep(0.0, 1.0, f.x)),
  //			mix(c, d, smoothstep(0.0, 1.0, f.x)),
  //			smoothstep(0.0, 1.0, f.y)));

  // Same code, with the clamps in smoothstep and common subexpressions
  // optimized away.
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}


#endif // INCLUDE_RAND
