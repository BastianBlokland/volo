#include "binding.glsl"
#include "geometry.glsl"
#include "global.glsl"
#include "math_frag.glsl"
#include "tag.glsl"

bind_spec(0) const f32 s_heightNormalIntensity = 1.0;
bind_spec(1) const f32 s_splat1UvScale         = 50;
bind_spec(2) const f32 s_splat2UvScale         = 50;

bind_draw_img(0) uniform sampler2D u_texHeight;

bind_graphic_img(0) uniform sampler2D u_texSplat;
bind_graphic_img(1) uniform sampler2D u_tex1Color;
bind_graphic_img(2) uniform sampler2D u_tex1Rough;
bind_graphic_img(3) uniform sampler2D u_tex1Normal;
bind_graphic_img(4) uniform sampler2D u_tex2Color;
bind_graphic_img(5) uniform sampler2D u_tex2Rough;
bind_graphic_img(6) uniform sampler2D u_tex2Normal;

bind_internal(0) in flat f32 in_size;
bind_internal(1) in flat f32 in_heightScale;
bind_internal(2) in f32v2 in_texcoord;
bind_internal(3) in f32v3 in_worldPos;

bind_internal(0) out f32v4 out_base;
bind_internal(1) out f32v2 out_normal;
bind_internal(2) out f32v2 out_attribute;
bind_internal(3) out f32v3 out_emissive;

/**
 * Calculate the normal by taking samples around this location and normalizing the deltas.
 */
f32v3 heightmap_normal(const f32v2 uv, const f32 size, const f32 heightScale) {
  const f32 hLeft  = textureOffset(u_texHeight, uv, i32v2(-1, 0)).r * heightScale;
  const f32 hRight = textureOffset(u_texHeight, uv, i32v2(1, 0)).r * heightScale;
  const f32 hDown  = textureOffset(u_texHeight, uv, i32v2(0, -1)).r * heightScale;
  const f32 hUp    = textureOffset(u_texHeight, uv, i32v2(0, 1)).r * heightScale;

  const f32 xzScale = size / textureSize(u_texHeight, 0).x;
  return normalize(f32v3(hLeft - hRight, xzScale * 2.0, hDown - hUp));
}

/**
 * Sample a texture at multiple texcoord frequencies to hide visible tiling patterns.
 */
f32v4 texture_multi(const sampler2D s, const f32v2 texcoord) {
  // TODO: Investigate different blending techniques.
  return mix(texture(s, texcoord), texture(s, texcoord * -0.25), 0.5);
}

void main() {
  const f32v4 splat = texture(u_texSplat, in_texcoord);

  // Output base.
  GeoBase base;
  base.tags  = 1 << tag_terrain_bit;
  base.color = f32v3(0);
  base.color += splat.r * texture_multi(u_tex1Color, in_texcoord * s_splat1UvScale).rgb;
  base.color += splat.g * texture_multi(u_tex2Color, in_texcoord * s_splat2UvScale).rgb;
  out_base = geo_base_encode(base);

  // Output attributes.
  GeoAttribute attr;
  attr.roughness = 0;
  attr.roughness += splat.r * texture_multi(u_tex1Rough, in_texcoord * s_splat1UvScale).r;
  attr.roughness += splat.g * texture_multi(u_tex2Rough, in_texcoord * s_splat2UvScale).r;
  attr.metalness = 0;
  out_attribute = geo_attr_encode(attr);

  // Sample the detail-normal based on the splat-map.
  f32v3 splatNormRaw = f32v3(0, 0, 0);
  splatNormRaw += splat.r * texture_multi(u_tex1Normal, in_texcoord * s_splat1UvScale).xyz;
  splatNormRaw += splat.g * texture_multi(u_tex2Normal, in_texcoord * s_splat2UvScale).xyz;
  const f32v3 splatNorm = normal_tex_decode(splatNormRaw);

  // Compute the world-normal based on the normal map and the sampled detail normals.
  const f32v3 baseNormal = heightmap_normal(in_texcoord, in_size, in_heightScale);

  // Output normal.
  f32v3 normal = math_perturb_normal(splatNorm, baseNormal, in_worldPos, in_texcoord);
  out_normal   = geo_normal_encode(normal);

  // Output emissive.
  out_emissive = f32v3(0);
}
