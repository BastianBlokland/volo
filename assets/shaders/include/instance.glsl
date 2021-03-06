#ifndef INCLUDE_INSTANCE
#define INCLUDE_INSTANCE

#include "types.glsl"

const u32 c_maxInstances = 2048;
const u32 c_maxJoints    = 75; // Needs to match the maximum in rend_instance.c

struct InstanceData {
  f32v4 posAndScale;    // x, y, z position, w scale
  f32v4 rot;            // x, y, z, w rotation quaternion
  u32v4 tagsAndPadding; // x tag bits.
};

struct InstanceSkinnedData {
  f32v4 posAndScale;             // x, y, z position, w scale
  f32v4 rot;                     // x, y, z, w rotation quaternion
  u32v4 tagsAndPadding;          // x tag bits.
  f32m4 jointDelta[c_maxJoints]; // Transformation matrices relative to the bind pose.
};

/**
 * Compute a skinning matrix that blends between 4 joints.
 */
f32m4 instance_skin_mat(
    const InstanceSkinnedData data, const u32v4 jointIndices, const f32v4 jointWeights) {
  return jointWeights.x * data.jointDelta[jointIndices.x] +
         jointWeights.y * data.jointDelta[jointIndices.y] +
         jointWeights.z * data.jointDelta[jointIndices.z] +
         jointWeights.w * data.jointDelta[jointIndices.w];
}

#endif // INCLUDE_INSTANCE
