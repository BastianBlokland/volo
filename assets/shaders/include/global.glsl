#ifndef INCLUDE_GLOBAL
#define INCLUDE_GLOBAL

#include "types.glsl"

struct GlobalData {
  f32m4 view, viewInv;
  f32m4 proj, projInv;
  f32m4 viewProj, viewProjInv;
  f32v4 camPosition; // x, y, z position
  f32v4 camRotation; // x, y, z, w quaternion
  f32v4 resolution;  // x: width, y: height, z: aspect ratio (width / height), w: unused.
  f32v4 time;        // x: time seconds, y: real-time seconds, z, w: unused.
};

#endif // INCLUDE_GLOBAL
