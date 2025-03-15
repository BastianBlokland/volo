#ifndef INCLUDE_GEOMETRY
#define INCLUDE_GEOMETRY

#include "math.glsl"
#include "tag.glsl"
#include "texture.glsl"

/**
 * Geometry Buffer.
 *
 * Textures:
 * - Data0: (srgb rgba32):   rgb: color,    a: unused.
 * - Data1: (srgb rgba32):   rgb: emissive, a: unused.
 * - Data2: (linear rgba32): rg: normal,    b: roughness, a: tags.
 */

struct Geometry {
  f32v3 color;
  f32v3 normal;
  f32v3 emissive;
  u32   tags;
  f32   roughness;
};

struct GeometryEncoded {
  f32v4 data0, data1, data2;
};

GeometryEncoded geometry_encode(const Geometry geo) {
  GeometryEncoded encoded;
  encoded.data0.rgb = geo.color;
  encoded.data1.rgb = geo.emissive;
  encoded.data2.rg  = math_normal_encode(geo.normal);
  encoded.data2.b   = geo.roughness;
  encoded.data2.a   = tags_tex_encode(geo.tags); // NOTE: Only the first 8 tags are preserved.
  return encoded;
}

Geometry geometry_decode(const GeometryEncoded encoded) {
  Geometry geo;
  geo.color     = encoded.data0.rgb;
  geo.emissive  = encoded.data1.rgb;
  geo.normal    = math_normal_decode(encoded.data2.rg);
  geo.roughness = encoded.data2.b;
  geo.tags      = tags_tex_decode(encoded.data2.a);
  return geo;
}

f32v3 geometry_decode_normal(const f32v4 geoData2) { return math_normal_decode(geoData2.rg); }

#endif // INCLUDE_GEOMETRY
