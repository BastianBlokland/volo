#ifndef INCLUDE_GEOMETRY
#define INCLUDE_GEOMETRY

#include "math.glsl"
#include "types.glsl"

struct GeoSurface {
  f32v3 position;
  f32v3 color;
  f32v3 normal;
  f32   roughness;
};

#endif // INCLUDE_GEOMETRY
