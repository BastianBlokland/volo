#ifndef INCLUDE_TEXTURE
#define INCLUDE_TEXTURE

#include "types.glsl"

/**
 * Decode a srgb encoded color to a linear color.
 * NOTE: Fast approximation, more info: https://en.wikipedia.org/wiki/SRGB
 */
f32_vec3 decode_srgb_aprox(const f32_vec3 color) {
  const f32 inverseGamma = 2.2;
  return pow(color, f32_vec3(inverseGamma));
}

/**
 * Encode a linear color to srgb.
 * NOTE: Fast approximation, more info: https://en.wikipedia.org/wiki/SRGB
 */
f32_vec3 encode_srgb_aprox(const f32_vec3 color) {
  const f32 gamma = 1.0 / 2.2;
  return pow(color, f32_vec3(gamma));
}

/**
 * Sample a srgb encoded texture.
 */
f32_vec4 texture_srgb(sampler2D imageSampler, const f32_vec2 texcoord) {
  const f32_vec4 raw = texture(imageSampler, texcoord);
  return f32_vec4(decode_srgb_aprox(raw.rgb), raw.a);
}

#endif // INCLUDE_TEXTURE
