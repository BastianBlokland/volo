#pragma once
#include "core_format.h"
#include "geo_vector.h"

/**
 * Geometric quaternion.
 * Describes a rotation in 3 dimensional space.
 */

typedef union {
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
 * Compute a quaternion that rotates around an axis.
 * NOTE: Angle is in radians.
 */
GeoQuat geo_quat_angle_axis(GeoVector axis, f32 angle);

/**
 * Compute a 'difference' quaternion.
 */
GeoQuat geo_quat_from_to(GeoQuat from, GeoQuat to);

/**
 * Calculate the angle in radians that the given quaternion represents.
 */
f32 geo_quat_angle(GeoQuat);

/**
 *  Compute a quaternion that combines both quaternions.
 */
GeoQuat geo_quat_mul(GeoQuat a, GeoQuat b);

/**
 * Compute a vector that is rotated based on the given quaternion.
 */
GeoVector geo_quat_rotate(GeoQuat, GeoVector);

/**
 * Compute the inverse of the given quaternion.
 */
GeoQuat geo_quat_inv(GeoQuat);

/**
 * Calculate a normalized version of this quaternion (magnitude of 1).
 * Pre-condition: quaternion != 0
 */
GeoQuat geo_quat_norm(GeoQuat);

/**
 * Create a formatting argument for a quaternion.
 * NOTE: _QUAT_ is expanded multiple times, so care must be taken when providing complex
 * expressions.
 */
#define geo_quat_fmt(_QUAT_)                                                                       \
  fmt_list_lit(                                                                                    \
      fmt_float((_QUAT_).x), fmt_float((_QUAT_).y), fmt_float((_QUAT_).z), fmt_float((_QUAT_).w))
