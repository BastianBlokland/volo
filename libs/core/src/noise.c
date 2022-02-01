#include "core_math.h"
#include "core_noise.h"

extern const u8 g_perlinPermutations[512];

static f32 perlin_fade(const f32 t) { return t * t * t * (t * (t * 6 - 15) + 10); }

static f32 perlin_lerp(const f32 t, const f32 a, const f32 b) { return a + t * (b - a); }

static f32 perlin_grad(const i32 hash, const f32 x, const f32 y, const f32 z) {
  // Convert lower 4 bits of hash code into 12 gradient directions.
  const i32 h = hash & 15;
  const f32 u = h < 8 ? x : y;
  const f32 v = h < 4 ? y : h == 12 || h == 14 ? x : z;
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

f32 noise_perlin3(f32 x, f32 y, f32 z) {
  /**
   * 3d perlin noise.
   * Based on the original Java implemention by Ken Perlin: https://mrl.cs.nyu.edu/~perlin/noise/
   */
  // clang-format off

  // Find the unit cube that contains the point.
  const i32 iX = (i32)math_floor_f32(x) & 255;
  const i32 iY = (i32)math_floor_f32(y) & 255;
  const i32 iZ = (i32)math_floor_f32(z) & 255;

  // Find relative x,y,z of point in cube.
  x -= math_floor_f32(x);
  y -= math_floor_f32(y);
  z -= math_floor_f32(z);

  // Compute fade curves for each of x,y,z.
  const f32 u = perlin_fade(x);
  const f32 v = perlin_fade(y);
  const f32 w = perlin_fade(z);


  // Hash coordinates of the 8 cube corners.
  const i32 A  = g_perlinPermutations[iX   ] + iY,  AA = g_perlinPermutations[A     ] + iZ;
  const i32 AB = g_perlinPermutations[A + 1] + iZ,  B  = g_perlinPermutations[iX + 1] + iY;
  const i32 BA = g_perlinPermutations[B    ] + iZ,  BB = g_perlinPermutations[B  + 1] + iZ;

  // And add blended results from the 8 corners of the cube.
  return perlin_lerp(w, perlin_lerp(v, perlin_lerp(u, perlin_grad(g_perlinPermutations[AA], x, y, z),
                                                      perlin_grad(g_perlinPermutations[BA], x - 1, y, z)),
                                       perlin_lerp(u, perlin_grad(g_perlinPermutations[AB], x, y - 1, z),
                                                      perlin_grad(g_perlinPermutations[BB], x - 1, y - 1, z))),
                        perlin_lerp(v, perlin_lerp(u, perlin_grad(g_perlinPermutations[AA + 1], x, y, z - 1),
                                                      perlin_grad(g_perlinPermutations[BA + 1], x - 1, y, z - 1)),
                                       perlin_lerp(u, perlin_grad(g_perlinPermutations[AB + 1], x, y - 1, z - 1),
                                                      perlin_grad(g_perlinPermutations[BB + 1], x - 1, y - 1, z - 1))));
  // clang-format on
}
