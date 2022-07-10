#pragma once
#include "geo_line.h"
#include "geo_ray.h"

/**
 * Capsule in 3 dimensional space.
 */

typedef struct {
  GeoLine line;
  f32     radius;
} GeoCapsule;

/**
 * Compute the intersection of the capsule with the given ray.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 * NOTE: Writes the surface-normal at the point of intersection to 'outNormal'.
 */
f32 geo_capsule_intersect_ray(const GeoCapsule*, const GeoRay*, GeoVector* outNormal);
