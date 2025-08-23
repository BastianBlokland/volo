#include "core/math.h"
#include "core/noise.h"

static const u8 g_perlinPermutations[512] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103,
    30,  69,  142, 8,   99,  37,  240, 21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,
    197, 62,  94,  252, 219, 203, 117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174,
    20,  125, 136, 171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146, 158, 231,
    83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,  46,  245, 40,  244, 102, 143,
    54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,  18,  169, 200, 196,
    135, 130, 116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124,
    123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,  58,  17,
    182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101,
    155, 167, 43,  172, 9,   129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185,
    112, 104, 218, 246, 97,  228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,
    51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181, 199, 106, 157, 184, 84,  204, 176,
    115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,  222, 114, 67,  29,  24,  72,  243,
    141, 128, 195, 78,  66,  215, 61,  156, 180, 151, 160, 137, 91,  90,  15,  131, 13,  201, 95,
    96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240, 21,  10,  23,
    190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203, 117, 35,  11,  32,
    57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125, 136, 171, 168, 68,  175, 74,  165, 71,
    134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105,
    92,  41,  55,  46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209,
    76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130, 116, 188, 159, 86,  164, 100, 109, 198,
    173, 186, 3,   64,  52,  217, 226, 250, 124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,
    212, 207, 206, 59,  227, 47,  16,  58,  17,  182, 189, 28,  42,  223, 183, 170, 213, 119, 248,
    152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,   129, 22,  39,  253, 19,
    98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,  242, 193,
    238, 210, 144, 12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214,
    31,  181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236,
    205, 93,  222, 114, 67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180,
};

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
  const i32 iX = (i32)math_round_down_f32(x) & 255;
  const i32 iY = (i32)math_round_down_f32(y) & 255;
  const i32 iZ = (i32)math_round_down_f32(z) & 255;

  // Find relative x,y,z of point in cube.
  x -= math_round_down_f32(x);
  y -= math_round_down_f32(y);
  z -= math_round_down_f32(z);

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
