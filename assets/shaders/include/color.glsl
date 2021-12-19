#ifndef INCLUDE_COLOR
#define INCLUDE_COLOR

#include "types.glsl"

#define color_white f32_vec4(1.0, 1.0, 1.0, 1.0)
#define color_black f32_vec4(0.0, 0.0, 0.0, 1.0)
#define color_clear f32_vec4(0.0, 0.0, 0.0, 0.0)
#define color_silver f32_vec4(0.75, 0.75, 0.75, 1.0)
#define color_gray f32_vec4(0.5, 0.5, 0.5, 1.0)
#define color_red f32_vec4(1.0, 0.0, 0.0, 1.0)
#define color_maroon f32_vec4(0.5, 0.0, 0.0, 1.0)
#define color_yellow f32_vec4(1.0, 1.0, 0.0, 1.0)
#define color_olive f32_vec4(0.5, 0.5, 0.0, 1.0)
#define color_lime f32_vec4(0.0, 1.0, 0.0, 1.0)
#define color_green f32_vec4(0.0, 0.5, 0.0, 1.0)
#define color_aqua f32_vec4(0.0, 1.0, 1.0, 1.0)
#define color_teal f32_vec4(0.0, 0.5, 0.5, 1.0)
#define color_blue f32_vec4(0.0, 0.0, 1.0, 1.0)
#define color_navy f32_vec4(0.0, 0.0, 0.5, 1.0)
#define color_fuchsia f32_vec4(1.0, 0.0, 1.0, 1.0)
#define color_purple f32_vec4(0.5, 0.0, 0.5, 1.0)
#define color_soothing_purple f32_vec4(0.188, 0.039, 0.141, 1.0)

/**
 * Decode a srgb encoded color to a linear color.
 * NOTE: Fast approximation, more info: https://en.wikipedia.org/wiki/SRGB
 */
f32_vec3 color_decode_srgb(const f32_vec3 color) {
  const f32 inverseGamma = 2.2;
  return pow(color, f32_vec3(inverseGamma));
}

/**
 * Encode a linear color to srgb.
 * NOTE: Fast approximation, more info: https://en.wikipedia.org/wiki/SRGB
 */
f32_vec3 color_encode_srgb(const f32_vec3 color) {
  const f32 gamma = 1.0 / 2.2;
  return pow(color, f32_vec3(gamma));
}

#endif // INCLUDE_COLOR
