#ifndef INCLUDE_INSTANCE
#define INCLUDE_INSTANCE

#include "types.glsl"

const u32 c_maxInstances = 2048;
const u32 c_maxJoints    = 16; // Needs to match the maximum in asset_mesh.h

struct InstanceData {
  f32v4 posAndScale; // x, y, z position, w scale
  f32v4 rot;         // x, y, z, w rotation quaternion
};

struct InstanceSkinnedData {
  f32v4 posAndScale;                  // x, y, z position, w scale
  f32v4 rot;                          // x, y, z, w rotation quaternion
  f32m4 jointTransforms[c_maxJoints]; // Transformation matrix per joint.
};

#endif // INCLUDE_INSTANCE
