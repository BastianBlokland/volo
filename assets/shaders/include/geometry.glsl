#ifndef INCLUDE_GEOMETRY
#define INCLUDE_GEOMETRY

#include "math.glsl"
#include "tags.glsl"
#include "texture.glsl"
#include "types.glsl"

struct GeoSurface {
  f32v3 positionClip; // Clip-space.
  f32v3 position;     // World-space.
  f32v3 color;
  f32v3 normal;
  f32   roughness;
  u32   tags;
};

GeoSurface geo_surface_load(
    const sampler2D geoColorRough,
    const sampler2D geoNormalTags,
    const sampler2D geoDepth,
    const f32v2     coord,
    const f32m4     viewProjInv) {
  const f32v4 colorRough = texture(geoColorRough, coord);
  const f32v4 normalTags = texture(geoNormalTags, coord);
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

#endif // INCLUDE_GEOMETRY
