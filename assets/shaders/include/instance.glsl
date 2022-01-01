#ifndef INCLUDE_INSTANCE
#define INCLUDE_INSTANCE

#include "types.glsl"

const u32 c_maxInstances = 2048;

struct InstanceData {
  f32_vec4 position; // x, y, z position
  f32_vec4 rotation; // x, y, z, w rotation quaternion
};

#endif // INCLUDE_INSTANCE
