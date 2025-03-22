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

bind_internal(0) out f32v4 out_data0;
bind_internal(1) out f32v2 out_data1;
bind_internal(2) out f32v2 out_data2;
bind_internal(3) out f32v3 out_data3;

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

  Geometry geo;
  geo.tags     = 1 << tag_terrain_bit;
  geo.emissive = f32v3(0);

  // Sample the color based on the splat-map.
  geo.color = f32v3(0);
  geo.color += splat.r * texture_multi(u_tex1Color, in_texcoord * s_splat1UvScale).rgb;
  geo.color += splat.g * texture_multi(u_tex2Color, in_texcoord * s_splat2UvScale).rgb;

  // Sample the roughness based on the splat-map.
  geo.roughness = 0;
  geo.roughness += splat.r * texture_multi(u_tex1Rough, in_texcoord * s_splat1UvScale).a;
  geo.roughness += splat.g * texture_multi(u_tex2Rough, in_texcoord * s_splat2UvScale).a;

  // Sample the detail-normal based on the splat-map.
  f32v3 splatNormRaw = f32v3(0, 0, 0);
  splatNormRaw += splat.r * texture_multi(u_tex1Normal, in_texcoord * s_splat1UvScale).xyz;
  splatNormRaw += splat.g * texture_multi(u_tex2Normal, in_texcoord * s_splat2UvScale).xyz;
  const f32v3 splatNorm = normal_tex_decode(splatNormRaw);

  // Compute the world-normal based on the normal map and the sampled detail normals.
  const f32v3 baseNormal = heightmap_normal(in_texcoord, in_size, in_heightScale);

  // Output world normal.
  geo.normal = math_perturb_normal(splatNorm, baseNormal, in_worldPos, in_texcoord);

  const GeometryEncoded encoded = geometry_encode(geo);
  out_data0                     = encoded.data0;
  out_data1                     = encoded.data1;
  out_data2                     = encoded.data2;
  out_data3                     = encoded.data3;
}
