#ifndef INCLUDE_GEOMETRY
#define INCLUDE_GEOMETRY

#include "math.glsl"
#include "tags.glsl"
#include "texture.glsl"

/**
 * Geometry Buffer.
 *
 * Textures:
 * - Data0: (srgb rgba32):   rgb: color, a: roughness.
 * - Data1: (linear rgba32): rgb: normal, a: tags.
 */

struct Geometry {
  f32v3 color;
  f32v3 normal;
  u32   tags;
  f32   roughness;
};

struct GeometryEncoded {
  f32v4 data0, data1;
};

GeometryEncoded geometry_encode(const Geometry geo) {
  GeometryEncoded encoded;
  encoded.data0.rgb = geo.color;
  encoded.data0.a   = geo.roughness;
  encoded.data1.rgb = normal_tex_encode(geo.normal);
  encoded.data1.a   = tags_tex_encode(geo.tags); // NOTE: Only the first 8 tags are preserved.
  return encoded;
}

Geometry geometry_decode(const GeometryEncoded encoded) {
  Geometry geo;
  geo.color     = encoded.data0.rgb;
  geo.roughness = encoded.data0.a;
  geo.normal    = normal_tex_decode(encoded.data1.rgb);
  geo.tags      = tags_tex_decode(encoded.data1.a);
  return geo;
}

f32v3 geometry_decode_normal(const f32v4 geoData1) { return normal_tex_decode(geoData1.rgb); }

#endif // INCLUDE_GEOMETRY
