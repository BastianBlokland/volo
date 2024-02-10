#pragma once
#include "geo_vector.h"

/**
 * Geometric quaternion.
 * Describes a rotation in 3 dimensional space.
 */

typedef union uGeoQuat {
  struct {
    f32 x, y, z, w;
  };
  ALIGNAS(16) f32 comps[4];
} GeoQuat;

ASSERT(sizeof(GeoQuat) == 16, "GeoQuat has to be 128 bits");
ASSERT(alignof(GeoQuat) == 16, "GeoQuat has to be aligned to 128 bits");

/**
 * Identity quaternion.
 * Represents no rotation.
 */
#define geo_quat_ident ((GeoQuat){0, 0, 0, 1})

/**
 * Common rotation presets.
 */
#define geo_quat_forward_to_right ((GeoQuat){0, 0.7071068f, 0, 0.7071068f})
#define geo_quat_forward_to_left ((GeoQuat){0, -0.7071068f, 0, 0.7071068f})
#define geo_quat_forward_to_up ((GeoQuat){-0.7071068f, 0, 0, 0.7071068f})
#define geo_quat_forward_to_down ((GeoQuat){0.7071068f, 0, 0, 0.7071068f})
#define geo_quat_forward_to_forward ((GeoQuat){0, 0, 0, 1})
#define geo_quat_forward_to_backward ((GeoQuat){0, 1, 0, 0})
#define geo_quat_up_to_forward ((GeoQuat){0.7071068f, 0, 0, 0.7071068f})

/**
 * Compute a quaternion that rotates around an axis.
 * NOTE: Angle is in radians.
 * Pre-condition: axis is normalized.
 */
GeoQuat geo_quat_angle_axis(GeoVector axis, f32 angle);

/**
 * Compute a 'difference' quaternion.
 */
GeoQuat geo_quat_from_to(GeoQuat from, GeoQuat to);

/**
 * Compute a quaternion that combines both quaternions.
 */
GeoQuat geo_quat_mul(GeoQuat a, GeoQuat b);

/**
 * Compute a quaternion where each component is multiplied by the matching component of the vector.
 */
GeoQuat geo_quat_mul_comps(GeoQuat, GeoVector);

/**
 * Compute a vector that is rotated based on the given quaternion.
 */
GeoVector geo_quat_rotate(GeoQuat, GeoVector);

/**
 * Compute the inverse of the given quaternion.
 */
GeoQuat geo_quat_inverse(GeoQuat);

/**
 * Flip the given quaternions (represents the same rotation).
 */
GeoQuat geo_quat_flip(GeoQuat);

/**
 * Calculate a normalized version of this quaternion (magnitude of 1).
 * Pre-condition: quaternion != 0
 */
GeoQuat geo_quat_norm(GeoQuat);
GeoQuat geo_quat_norm_or_ident(GeoQuat);

/**
 * Calculate the dot product of two quaternions.
 */
f32 geo_quat_dot(GeoQuat a, GeoQuat b);

/**
 * Computes a quaternion that rotates from the identity axes to a new axis system.
 * NOTE: Vectors do not need to be normalized, but should not be zero.
 * NOTE: Up does not need to be orthogonal to fwd as the up is reconstructed.
 */
GeoQuat geo_quat_look(GeoVector forward, GeoVector upRef);

/**
 * Calculate the spherically interpolated quaternion from x to y at time t.
 * NOTE: Does not compensate for double-cover (two quaternions representing the same rotation).
 */
GeoQuat geo_quat_slerp(GeoQuat a, GeoQuat b, f32 t);

/**
 * Rotate the quaternion towards the given target with a maximum step-size of maxAngle radians.
 * NOTE: Returns true if we've reached the target.
 */
bool geo_quat_towards(GeoQuat*, GeoQuat target, f32 maxAngle);

/**
 * Compute a quaternion based on the given euler angles (in radians).
 */
GeoQuat geo_quat_from_euler(GeoVector);

/**
 * Convert the given quaternion to euler angles (in radians).
 */
GeoVector geo_quat_to_euler(GeoQuat q);

/**
 * Convert the given quaternion to a combined angle-axis.
 * NOTE: The angle is the magnitude of the combined angle-axis.
 */
GeoVector geo_quat_to_angle_axis(GeoQuat);

typedef struct {
  GeoQuat swing, twist;
} GeoSwingTwist;

/**
 * Convert the given quaternion to two concatenated rotations, swing and twist around an axis.
 */
GeoSwingTwist geo_quat_to_swing_twist(GeoQuat, GeoVector twistAxis);

/**
 * Clamp the quaternion so that the angle does not exceed 'maxAngle'.
 * NOTE: Returns 'true' if clamping applied otherwise 'false'.
 * Pre-condition: maxAngle >= 0
 */
bool geo_quat_clamp(GeoQuat*, f32 maxAngle);

/**
 * Pack a quaternion to 16 bit floats.
 */
void geo_quat_pack_f16(GeoQuat, f16 out[4]);

/**
 * Create a formatting argument for a quaternion.
 * NOTE: _QUAT_ is expanded multiple times, so care must be taken when providing complex
 * expressions.
 */
#define geo_quat_fmt(_QUAT_)                                                                       \
  fmt_list_lit(                                                                                    \
      fmt_float((_QUAT_).x), fmt_float((_QUAT_).y), fmt_float((_QUAT_).z), fmt_float((_QUAT_).w))
