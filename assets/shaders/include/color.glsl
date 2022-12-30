#ifndef INCLUDE_COLOR
#define INCLUDE_COLOR

#include "types.glsl"

// clang-format off
#define color_white           f32v4(1.0, 1.0, 1.0, 1.0)
#define color_black           f32v4(0.0, 0.0, 0.0, 1.0)
#define color_clear           f32v4(0.0, 0.0, 0.0, 0.0)
#define color_silver          f32v4(0.75, 0.75, 0.75, 1.0)
#define color_gray            f32v4(0.5, 0.5, 0.5, 1.0)
#define color_red             f32v4(1.0, 0.0, 0.0, 1.0)
#define color_maroon          f32v4(0.5, 0.0, 0.0, 1.0)
#define color_yellow          f32v4(1.0, 1.0, 0.0, 1.0)
#define color_olive           f32v4(0.5, 0.5, 0.0, 1.0)
#define color_lime            f32v4(0.0, 1.0, 0.0, 1.0)
#define color_green           f32v4(0.0, 0.5, 0.0, 1.0)
#define color_aqua            f32v4(0.0, 1.0, 1.0, 1.0)
#define color_teal            f32v4(0.0, 0.5, 0.5, 1.0)
#define color_blue            f32v4(0.0, 0.0, 1.0, 1.0)
#define color_navy            f32v4(0.0, 0.0, 0.5, 1.0)
#define color_fuchsia         f32v4(1.0, 0.0, 1.0, 1.0)
#define color_purple          f32v4(0.5, 0.0, 0.5, 1.0)
#define color_orange          f32v4(1.0, 0.5, 0.0, 1.0)
#define color_soothing_purple f32v4(0.188, 0.039, 0.141, 1.0)
// clang-format on

/**
 * Create a color from 8 bit rgba values.
 */
f32v4 color_from_u32(const u32 val) {
  return f32v4(
      f32((val >> 0) & 0xFF) / 255.0,
      f32((val >> 8) & 0xFF) / 255.0,
      f32((val >> 16) & 0xFF) / 255.0,
      f32((val >> 24) & 0xFF) / 255.0);
}

/**
 * Create a color from 'Hue', 'Saturation', 'Value' components.
 * Implementation based on: http://lolengine.net/blog/2013/07/27/rgb-to-hsv-in-glsl
 */
f32v3 color_from_hsv(const f32 h, const f32 s, const f32 v) {
  const f32v4 k = f32v4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  const f32v3 p = abs(fract(f32v3(h, h, h) + k.xyz) * 6.0 - k.www);
  return v * mix(k.xxx, clamp(p - k.xxx, 0.0, 1.0), s);
}

/**
 * Decode a srgb encoded color to a linear color.
 * NOTE: Fast approximation, more info: https://en.wikipedia.org/wiki/SRGB
 */
f32v3 color_decode_srgb(const f32v3 color) {
  const f32 inverseGamma = 2.2;
  return pow(color, f32v3(inverseGamma));
}

/**
 * Encode a linear color to srgb.
 * NOTE: Fast approximation, more info: https://en.wikipedia.org/wiki/SRGB
 */
f32v3 color_encode_srgb(const f32v3 color) {
  const f32 gamma = 1.0 / 2.2;
  return pow(color, f32v3(gamma));
}

#endif // INCLUDE_COLOR
