#ifndef INCLUDE_TEXTURE
#define INCLUDE_TEXTURE

#include "color.glsl"
#include "types.glsl"

/**
 * Sample a linear encoded texture.
 */
f32_vec4 texture_sample_linear(const sampler2D imageSampler, const f32_vec2 texcoord) {
  return texture(imageSampler, texcoord);
}

/**
 * Sample a srgb encoded texture.
 */
f32_vec4 texture_sample_srgb(const sampler2D imageSampler, const f32_vec2 texcoord) {
  const f32_vec4 raw = texture_sample_linear(imageSampler, texcoord);
  return f32_vec4(color_decode_srgb(raw.rgb), raw.a);
}

/**
 * Sample a normal texture in tangent space and convert it to the axis system formed by the given
 * normal and tangent references, 'w' component of the tangent indicates the handedness.
 * NOTE: normalRef and tangentRef are not required to be unit vectors.
 */
f32_vec3 texture_sample_normal(
    const sampler2D normalSampler,
    const f32_vec2  texcoord,
    const f32_vec3  normalRef,
    const f32_vec4  tangentRef) {

  const f32_vec3 tangent   = normalize(tangentRef.xyz);
  const f32_vec3 normal    = normalize(normalRef);
  const f32_vec3 bitangent = normalize(cross(normal, tangent) * tangentRef.w);
  const f32_mat3 rotMatrix = f32_mat3(tangent, bitangent, normal);
  return rotMatrix * normalize(texture(normalSampler, texcoord).xyz * 2.0 - 1.0);
}

#endif // INCLUDE_TEXTURE
