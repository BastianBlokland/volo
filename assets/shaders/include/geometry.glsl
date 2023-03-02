#ifndef INCLUDE_GEOMETRY
#define INCLUDE_GEOMETRY

#include "math.glsl"
#include "tags.glsl"
#include "texture.glsl"
#include "types.glsl"

/**
 * Geometry Buffer.
 *
 * Textures:
 * - Data0: (srgb rgba32):   rgb: color, a: roughness.
 * - Data1: (linear rgba32): rgb: normal, a: tags.
 * - Depth: (f32)
 */

struct GeoSurface {
  f32v3 positionClip; // Clip-space.
  f32v3 position;     // World-space.
  f32v3 color;
  f32v3 normal;
  f32   roughness;
  u32   tags;
};

GeoSurface geo_surface_load(
    const sampler2D geoData0,
    const sampler2D geoData1,
    const sampler2D geoDepth,
    const f32v2     coord,
    const f32m4     viewProjInv) {
  const f32v4 colorRough = texture(geoData0, coord);
  const f32v4 normalTags = texture(geoData1, coord);
  const f32   depth      = texture(geoDepth, coord).r;

  const u32   tags        = tags_tex_decode(normalTags.w);
  const f32v3 clipPos     = f32v3(coord * 2.0 - 1.0, depth);
  const f32v4 worldPosRaw = viewProjInv * f32v4(clipPos, 1);
  const f32v3 worldPos    = worldPosRaw.xyz / worldPosRaw.w; // Perspective divide.

  GeoSurface surf;
  surf.positionClip = clipPos;
  surf.position     = worldPos;
  surf.color        = colorRough.rgb;
  surf.normal       = normal_tex_decode(normalTags.xyz);
  surf.roughness    = colorRough.a;
  surf.tags         = tags;
  return surf;
}

f32v3 geo_surface_load_normal(const sampler2D geoData1, const f32v2 coord) {
  const f32v4 normalTags = texture(geoData1, coord);
  return normal_tex_decode(normalTags.xyz);
}

#endif // INCLUDE_GEOMETRY
