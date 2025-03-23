#ifndef INCLUDE_GEOMETRY
#define INCLUDE_GEOMETRY

#include "math.glsl"
#include "tag.glsl"
#include "texture.glsl"

/**
 * Geometry Buffer.
 *
 * Textures:
 * - GeoBase:      (srgb rgba32):  [r] color     [g] color     [b] color    [a] tags
 * - GeoNormal:    (linear rg16):  [r] normal    [g] normal
 * - GeoAttribute: (linear rg16):  [r] roughness [g] metalness
 * - GeoEmissive:  (linear rgb16): [r] emissive  [g] emissive  [b] emissive
 */

struct GeoBase {
  f32v3 color;
  u32   tags;
};

struct GeoAttribute {
  f32 roughness, metalness;
};

f32v4 geo_base_encode(const GeoBase base) {
  // NOTE: Only the first 8 tags are preserved.
  return f32v4(base.color, tags_tex_encode(base.tags));
}

GeoBase geo_base_decode(const f32v4 data) {
  GeoBase base;
  base.color = data.rgb;
  base.tags  = tags_tex_decode(data.a);
  return base;
}

f32v2 geo_normal_encode(const f32v3 normal) { return math_normal_encode(normal); }

f32v3 geo_normal_decode(const f32v2 data) { return math_normal_decode(data); }

f32v2 geo_attr_encode(const GeoAttribute attr) { return f32v2(attr.roughness, attr.metalness); }

GeoAttribute geo_attr_decode(const f32v2 data) {
  GeoAttribute attr;
  attr.roughness = data.r;
  attr.metalness = data.g;
  return attr;
}

#endif // INCLUDE_GEOMETRY
