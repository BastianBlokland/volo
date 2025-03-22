#ifndef INCLUDE_GEOMETRY
#define INCLUDE_GEOMETRY

#include "math.glsl"
#include "tag.glsl"
#include "texture.glsl"

/**
 * Geometry Buffer.
 *
 * Textures:
 * - Data0: (srgb rgba32):  [r] color     [g] color     [b] color    [a] tags
 * - Data1: (linear rg16):  [r] normal    [g] normal
 * - Data2: (linear rg16):  [r] roughness [g] unused
 * - Data3: (linear rgb16): [r] emissive  [g] emissive  [b] emissive
 */

struct Geometry {
  f32v3 color;
  f32v3 normal;
  f32v3 emissive;
  u32   tags;
  f32   roughness;
};

struct GeometryEncoded {
  f32v4 data0;
  f32v2 data1;
  f32v2 data2;
  f32v3 data3;
};

GeometryEncoded geometry_encode(const Geometry geo) {
  GeometryEncoded encoded;
  encoded.data0.rgb = geo.color;
  encoded.data0.a   = tags_tex_encode(geo.tags); // NOTE: Only the first 8 tags are preserved.
  encoded.data1.rg  = math_normal_encode(geo.normal);
  encoded.data2.r   = geo.roughness;
  encoded.data3.rgb = geo.emissive;
  return encoded;
}

Geometry geometry_decode(const GeometryEncoded encoded) {
  Geometry geo;
  geo.color     = encoded.data0.rgb;
  geo.tags      = tags_tex_decode(encoded.data0.a);
  geo.normal    = math_normal_decode(encoded.data1.rg);
  geo.roughness = encoded.data2.r;
  geo.emissive  = encoded.data3.rgb;
  return geo;
}

f32v3 geometry_decode_normal(const f32v4 geoData1) { return math_normal_decode(geoData1.rg); }

#endif // INCLUDE_GEOMETRY
