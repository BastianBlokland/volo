#ifndef INCLUDE_GLOBAL
#define INCLUDE_GLOBAL

#include "types.glsl"

struct GlobalData {
  f32v4 resolution; // x, y size, z, w invSize
  f32m4 viewProj;
  f32v4 camPosition; // x, y, z position
  f32v4 camRotation; // x, y, z, w quaternion
};

#endif // INCLUDE_GLOBAL
