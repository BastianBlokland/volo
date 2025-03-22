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
 * - GeoAttribute: (linear rg16):  [r] roughness [g] unused
 * - GeoEmissive:  (linear rgb16): [r] emissive  [g] emissive  [b] emissive
 */

struct Geometry {
  f32v3 color;
  f32v3 normal;
  f32v3 emissive;
  u32   tags;
  f32   roughness;
};

struct GeometryEncoded {
  f32v4 base;
  f32v2 normal;
  f32v2 attr;
  f32v3 emissive;
};

GeometryEncoded geometry_encode(const Geometry geo) {
  GeometryEncoded encoded;
  encoded.base.rgb     = geo.color;
  encoded.base.a       = tags_tex_encode(geo.tags); // NOTE: Only the first 8 tags are preserved.
  encoded.normal.rg    = math_normal_encode(geo.normal);
  encoded.attr.r       = geo.roughness;
  encoded.emissive.rgb = geo.emissive;
  return encoded;
}

Geometry geometry_decode(const GeometryEncoded encoded) {
  Geometry geo;
  geo.color     = encoded.base.rgb;
  geo.tags      = tags_tex_decode(encoded.base.a);
  geo.normal    = math_normal_decode(encoded.normal.rg);
  geo.roughness = encoded.attr.r;
  geo.emissive  = encoded.emissive.rgb;
  return geo;
}

f32v3 geometry_decode_normal(const f32v4 geoNormal) { return math_normal_decode(geoNormal.rg); }

#endif // INCLUDE_GEOMETRY
