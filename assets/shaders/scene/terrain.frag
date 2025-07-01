#include "binding.glsl"
#include "geometry.glsl"
#include "global.glsl"
#include "math_frag.glsl"
#include "tag.glsl"

bind_spec(0) const u32 s_splatLayers           = 1;
bind_spec(1) const f32 s_splat1UvScale         = 50;
bind_spec(2) const f32 s_splat2UvScale         = 50;
bind_spec(3) const f32 s_splat3UvScale         = 50;
bind_spec(4) const f32 s_splat4UvScale         = 50;
bind_spec(5) const f32 s_heightNormalIntensity = 1.0;

bind_draw_img(0) uniform sampler2D u_texHeight;

bind_graphic_img(0) uniform sampler2D u_texSplat;
bind_graphic_img(1) uniform sampler2DArray u_texColor;
bind_graphic_img(2) uniform sampler2DArray u_texRough;
bind_graphic_img(3) uniform sampler2DArray u_texNormal;

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
f32v4 texture_multi(const sampler2DArray s, const f32v2 texcoord, const f32 layer) {
  // TODO: Investigate different blending techniques.
  return mix(texture(s, f32v3(texcoord, layer)), texture(s, f32v3(texcoord * -0.25, layer)), 0.5);
}

void main() {
  const f32 splatUvScale[] = {
    s_splat1UvScale,
    s_splat2UvScale,
    s_splat3UvScale,
    s_splat4UvScale,
  };
  const f32v4 splat = texture(u_texSplat, in_texcoord);

  GeoBase base;
  base.tags  = 1 << tag_terrain_bit;
  base.color = f32v3(0);

  GeoAttribute attr;
  attr.roughness = 0;
  attr.metalness = 0;

  f32v3 splatNormRaw = f32v3(0, 0, 0);

  // Sample the splat layers.
  for (u32 i = 0; i != s_splatLayers; ++i) {
    base.color += splat[i] * texture_multi(u_texColor, in_texcoord * splatUvScale[i], i).rgb;
    attr.roughness += splat[i] * texture_multi(u_texRough, in_texcoord * splatUvScale[i], i).r;
    splatNormRaw += splat[i] * texture_multi(u_texNormal, in_texcoord * splatUvScale[i], i).xyz;
  }

  // Output base.
  out_base = geo_base_encode(base);

  // Output attributes.
  out_attribute = geo_attr_encode(attr);

  // Compute the world-normal based on the normal map and the sampled detail normals.
  const f32v3 splatNorm = normal_tex_decode(splatNormRaw);
  const f32v3 baseNormal = heightmap_normal(in_texcoord, in_size, in_heightScale);

  // Output normal.
  f32v3 normal = math_perturb_normal(splatNorm, baseNormal, in_worldPos, in_texcoord);
  out_normal   = geo_normal_encode(normal);

  // Output emissive.
  out_emissive = f32v3(0);
}
