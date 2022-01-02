#ifndef INCLUDE_GLOBAL
#define INCLUDE_GLOBAL

#include "types.glsl"

struct GlobalData {
  f32_mat4 viewProj;
  f32_vec4 camPosition; // x, y, z position
  f32_vec4 camRotation; // x, y, z, w quaternion
};

#endif // INCLUDE_GLOBAL
