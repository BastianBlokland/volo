#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "tags.glsl"
#include "texture.glsl"

bind_spec(0) const f32 s_heightNormalIntensity = 1.0;
bind_spec(1) const f32 s_splat1UvScale         = 50;
bind_spec(2) const f32 s_splat2UvScale         = 50;

bind_graphic(1) uniform sampler2D u_texHeight;
bind_graphic(2) uniform sampler2D u_texSplat;
bind_graphic(3) uniform sampler2D u_tex1ColorRough;
bind_graphic(4) uniform sampler2D u_tex1Normal;
bind_graphic(5) uniform sampler2D u_tex2ColorRough;
bind_graphic(6) uniform sampler2D u_tex2Normal;

bind_internal(0) in flat f32 in_size;
bind_internal(1) in flat f32 in_heightScale;
bind_internal(2) in f32v2 in_texcoord;
bind_internal(3) in f32v3 in_worldPos;

bind_internal(0) out f32v4 out_colorRough;
bind_internal(1) out f32v4 out_normalTags;

/**
 * Calculate the normal by taking samples around this location and normalizing the deltas.
 */
f32v3 heightmap_normal(const f32v2 uv, const f32 size, const f32 heightScale) {
  const f32 hLeft  = textureOffset(u_texHeight, uv, i32v2(-1, 0)).r * heightScale;
  const f32 hRight = textureOffset(u_texHeight, uv, i32v2(1, 0)).r * heightScale;
  const f32 hDown  = textureOffset(u_texHeight, uv, i32v2(0, -1)).r * heightScale;
  const f32 hUp    = textureOffset(u_texHeight, uv, i32v2(0, 1)).r * heightScale;

  // NOTE: Assumes that the height map is a square texture.
  const f32 unitSize = 1.0 / textureSize(u_texHeight, 0).x * size;

  /**
   * Exaggerate the offset on the xz plane for more dramatic lighting.
   * TODO: Verify the math here to make sure we're not compensating for broken logic.
   */
  const f32 unitRefHeight = unitSize / s_heightNormalIntensity;

  return normalize(f32v3(hLeft - hRight, unitRefHeight * 2.0, hDown - hUp));
}

/**
 * Apply a tangent normal (from a normalmap for example) to a worldNormal.
 * The tangent and bitangent vectors are derived from change in position and texcoords.
 */
f32v3 perturbNormal(
    const f32v3 tangentNormal,
    const f32v3 worldNormal,
    const f32v3 worldPos,
    const f32v2 texcoord) {
  const f32v3 deltaPosX = dFdx(worldPos);
  const f32v3 deltaPosY = dFdy(worldPos);
  const f32v2 deltaTexX = dFdx(texcoord);
  const f32v2 deltaTexY = dFdy(texcoord);

  const f32v3 refNorm  = normalize(worldNormal);
  const f32v3 refTan   = normalize(deltaPosX * deltaTexY.t - deltaPosY * deltaTexX.t);
  const f32v3 refBitan = normalize(cross(refNorm, refTan));
  const f32m3 rot      = f32m3(refTan, refBitan, refNorm);

  return normalize(rot * tangentNormal);
}

/**
 * Sample a texture at multiple texcoord frequencies to hide visible tiling patterns.
 */
f32v4 textureMulti(const sampler2D s, const f32v2 texcoord) {
  // TODO: Investigate different blending techniques.
  return mix(texture(s, texcoord), texture(s, texcoord * -0.25), 0.5);
}

void main() {
  const f32v4 splat = texture(u_texSplat, in_texcoord);

  // Sample the color (and roughness) based on the splat-map.
  f32v4 splatColRough = f32v4(0);
  splatColRough += splat.r * textureMulti(u_tex1ColorRough, in_texcoord * s_splat1UvScale);
  splatColRough += splat.g * textureMulti(u_tex2ColorRough, in_texcoord * s_splat2UvScale);

  // Output color and roughness.
  out_colorRough = splatColRough;

  // Sample the detail-normal based on the splat-map.
  f32v3 splatNormRaw = f32v3(0, 0, 0);
  splatNormRaw += splat.r * textureMulti(u_tex1Normal, in_texcoord * s_splat1UvScale).xyz;
  splatNormRaw += splat.g * textureMulti(u_tex2Normal, in_texcoord * s_splat2UvScale).xyz;
  const f32v3 splatNorm = normal_tex_decode(splatNormRaw);

  // Compute the world-normal based on the normal map and the sampled detail normals.
  const f32v3 baseNormal = heightmap_normal(in_texcoord, in_size, in_heightScale);

  // Output world normal.
  const f32v3 normal = perturbNormal(splatNorm, baseNormal, in_worldPos, in_texcoord);
  out_normalTags.xyz = normal_tex_encode(normal);

  // Output tags.
  const u32 tags   = 1 << tag_terrain_bit;
  out_normalTags.w = tags_tex_encode(tags);
}
