#ifndef INCLUDE_QUAT
#define INCLUDE_QUAT

#include "types.glsl"

/**
 * Identity quaternion.
 * Represents no rotation.
 */
f32v4 quat_identity() { return f32v4(0, 0, 0, 1); }

/**
 * Compute a quaternion that rotates around an axis.
 * NOTE: Angle is in radians.
 * Pre-condition: Axis is normalized.
 */
f32v4 quat_angle_axis(const f32 angle, const f32v3 axis) {
  return f32v4(axis * sin(angle * 0.5), cos(angle * 0.5));
}

/**
 * Compute a vector that is rotated based on the given quaternion.
 * Pre-condition: Quaternion has to be normalized.
 */
f32v3 quat_rotate(const f32v4 quat, const f32v3 vec) {
  return vec + 2.0 * cross(quat.xyz, cross(quat.xyz, vec) + quat.w * vec);
}

/**
 * Compute the inverse of the given quaternion.
 */
f32v4 quat_inverse(const f32v4 quat) { return f32v4(-quat.xyz, quat.w); }

#endif // INCLUDE_QUAT
