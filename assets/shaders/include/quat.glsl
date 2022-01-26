#ifndef INCLUDE_QUAT
#define INCLUDE_QUAT

#include "types.glsl"

/**
 * Compute a vector that is rotated based on the given quaternion.
 * Pre-condition: Quaternion has to be normalized.
 */
f32v3 quat_rotate(const f32v4 quat, const f32v3 vec) {
  return vec + 2.0 * cross(quat.xyz, cross(quat.xyz, vec) + quat.w * vec);
}

#endif // INCLUDE_QUAT
