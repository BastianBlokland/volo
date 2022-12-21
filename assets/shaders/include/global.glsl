#ifndef INCLUDE_GLOBAL
#define INCLUDE_GLOBAL

#include "types.glsl"

struct GlobalData {
  f32m4 viewProj, viewProjInv;
  f32v4 camPosition; // x, y, z position
  f32v4 camRotation; // x, y, z, w quaternion
  f32   aspectRatio; // Output resolution width / height.
};

#endif // INCLUDE_GLOBAL
