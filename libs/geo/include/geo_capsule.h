#pragma once
#include "geo.h"
#include "geo_line.h"

/**
 * Capsule in 3 dimensional space.
 */

typedef struct sGeoCapsule {
  GeoLine line;
  f32     radius;
} GeoCapsule;

/**
 * Dilate the capsule by the given amount.
 */
GeoCapsule geo_capsule_dilate(const GeoCapsule*, f32 radius);

/**
 * Transform the given capsule.
 */
GeoCapsule geo_capsule_transform3(const GeoCapsule*, GeoVector offset, GeoQuat rotation, f32 scale);

/**
 * Compute the intersection of the capsule with the given ray.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 * NOTE: Writes the surface-normal at the point of intersection to 'outNormal'.
 */
f32 geo_capsule_intersect_ray(const GeoCapsule*, const GeoRay*);
f32 geo_capsule_intersect_ray_info(const GeoCapsule*, const GeoRay*, GeoVector* outNormal);

/**
 * Overlap tests.
 */
bool geo_capsule_overlap_sphere(const GeoCapsule*, const GeoSphere*);
