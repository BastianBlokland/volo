#pragma once
#include "geo_box.h"
#include "geo_quat.h"

/**
 * Geometric 3d rotated box.
 */

typedef struct {
  GeoBox  box;
  GeoQuat rotation;
} GeoBoxRotated;

/**
 * Compute the intersection of the rotated box with the given ray.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 * NOTE: Writes the surface-normal at the point of intersection to 'outNormal'.
 */
f32 geo_box_rotated_intersect_ray(const GeoBoxRotated*, const GeoRay*, GeoVector* outNormal);
