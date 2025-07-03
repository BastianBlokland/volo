#include "binding.glsl"
#include "geometry.glsl"
#include "global.glsl"
#include "math_frag.glsl"
#include "noise.glsl"
#include "tag.glsl"

bind_spec(0) const u32 s_splatLayers           = 1;
bind_spec(1) const f32 s_splatVariationScale   = 2.25;
bind_spec(2) const f32 s_splat1UvScale         = 50;
bind_spec(3) const f32 s_splat2UvScale         = 50;
bind_spec(4) const f32 s_splat3UvScale         = 50;
bind_spec(5) const f32 s_splat4UvScale         = 50;
bind_spec(6) const f32 s_heightNormalIntensity = 1.0;

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

f32v4 layer_tex(
    const sampler2DArray  tex,
    const f32             layer,
    const f32v2           texcoord,
    const f32v2           offsetA,
    const f32v2           offsetB,
    const f32             frac,
    const f32v2           dx,
    const f32v2           dy) {

  /**
   * Avoid visible texture repetition by blending between two (randomly picked) virtual patterns.
   * Source https://iquilezles.org/articles/texturerepetition/ by 'Inigo Quilez'.
   */

  // Sample the two virtual patterns.
  const f32v4 colorA = textureGrad(tex, f32v3(texcoord + offsetA, layer), dx, dy);
  const f32v4 colorB = textureGrad(tex, f32v3(texcoord + offsetB, layer), dx, dy);

  // Interpolate between the two virtual patterns.
  const f32v4 delta = colorA - colorB;
  const f32 blend = smoothstep(0.2, 0.8, frac - 0.1 * (delta.x + delta.y + delta.z));

  return mix(colorA, colorB, blend);
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
    const f32v2 pos = in_texcoord * splatUvScale[i];

    /**
    * Avoid visible texture repetition by blending between two (randomly picked) virtual patterns.
    * Source https://iquilezles.org/articles/texturerepetition/ by 'Inigo Quilez'.
    */

    // Pick a virtual pattern based on a 2d value noise.
    const f32   noiseVal   = noise_value_f32v2(pos * s_splatVariationScale) * 8.0;
    const f32   noiseIndex = floor(noiseVal);
    const f32   noiseFrac  = fract(noiseVal);

    // Offsets for the different virtual patterns
    const f32v2 offA = sin(f32v2(3.0, 7.0) * (noiseIndex + 0.0));
    const f32v2 offB = sin(f32v2(3.0, 7.0) * (noiseIndex + 1.0));

    // Derivatives for mip-mapping.
    const f32v2 dx = dFdx(pos), dy = dFdy(pos);

    base.color     += splat[i] * layer_tex(u_texColor, i, pos, offA, offB, noiseFrac, dx, dy).rgb;
    attr.roughness += splat[i] * layer_tex(u_texRough, i, pos, offA, offB, noiseFrac, dx, dy).r;
    splatNormRaw   += splat[i] * layer_tex(u_texNormal, i, pos, offA, offB, noiseFrac, dx, dy).xyz;
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
