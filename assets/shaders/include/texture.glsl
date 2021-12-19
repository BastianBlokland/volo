#ifndef INCLUDE_TEXTURE
#define INCLUDE_TEXTURE

#include "color.glsl"
#include "types.glsl"

/**
 * Sample a srgb encoded texture.
 */
f32_vec4 texture_sample_linear(sampler2D imageSampler, const f32_vec2 texcoord) {
  return texture(imageSampler, texcoord);
}

/**
 * Sample a srgb encoded texture.
 */
f32_vec4 texture_sample_srgb(sampler2D imageSampler, const f32_vec2 texcoord) {
  const f32_vec4 raw = texture(imageSampler, texcoord);
  return f32_vec4(color_decode_srgb(raw.rgb), raw.a);
}

#endif // INCLUDE_TEXTURE
