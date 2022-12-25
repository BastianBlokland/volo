#ifndef INCLUDE_TEXTURE
#define INCLUDE_TEXTURE

#include "color.glsl"
#include "types.glsl"

/**
 * Encode a normal (components in the -1 to 1 range) for storing in a texture (0 to 1 range).
 */
f32v3 normal_tex_encode(const f32v3 normal) { return normal * 0.5 + 0.5; }
f32v3 normal_tex_decode(const f32v3 normal) { return normalize(normal * 2.0 - 1.0); }

/**
 * Sample a cubemap.
 */
f32v4 texture_cube(const samplerCube tex, const f32v3 direction) {
  // NOTE: Flip the Y component as we are using the bottom as the texture origin.
  return texture(tex, f32v3(direction.x, -direction.y, direction.z));
}

/**
 * Sample a normal texture in tangent space and convert it to the axis system formed by the given
 * normal and tangent references, 'w' component of the tangent indicates the handedness.
 * NOTE: normalRef and tangentRef are not required to be unit vectors.
 */
f32v3 texture_normal(
    const sampler2D normalSampler,
    const f32v2     texcoord,
    const f32v3     normalRef,
    const f32v4     tangentRef) {
  const f32v3 tangent   = normalize(tangentRef.xyz);
  const f32v3 normal    = normalize(normalRef);
  const f32v3 bitangent = normalize(cross(tangent, normal) * tangentRef.w);
  const f32m3 rotMatrix = f32m3(tangent, bitangent, normal);
  return rotMatrix * normal_tex_decode(texture(normalSampler, texcoord).xyz);
}

#endif // INCLUDE_TEXTURE
