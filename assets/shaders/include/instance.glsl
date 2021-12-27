#ifndef INCLUDE_INSTANCE
#define INCLUDE_INSTANCE

#include "types.glsl"

const u32 g_maxInstances = 2048;

struct InstanceData {
  f32_mat4 matrix;
};

#endif // INCLUDE_INSTANCE
