#ifndef INCLUDE_INSTANCE
#define INCLUDE_INSTANCE

#include "types.glsl"

const u32 c_maxInstances = 2048;

struct InstanceData {
  f32v4 posAndScale; // x, y, z position, w scale
  f32v4 rot;         // x, y, z, w rotation quaternion
};

#endif // INCLUDE_INSTANCE
