#pragma once
#include "geo_box.h"
#include "geo_quat.h"

// Forward declare from 'geo_sphere.h'.
typedef struct sGeoSphere GeoSphere;

/**
 * Geometric 3d rotated box.
 */

typedef struct {
  GeoBox  box;
  GeoQuat rotation;
} GeoBoxRotated;

/**
 * Calculate the rotated bounding box of a Capsule.
 */
GeoBoxRotated geo_box_rotated_from_capsule(GeoVector bottom, GeoVector top, f32 radius);

/**
 * Compute the intersection of the rotated box with the given ray.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 * NOTE: Writes the surface-normal at the point of intersection to 'outNormal'.
 */
f32 geo_box_rotated_intersect_ray(const GeoBoxRotated*, const GeoRay*, GeoVector* outNormal);

/**
 * Overlap tests.
 */
bool geo_box_rotated_overlap_box(const GeoBoxRotated*, const GeoBox*);
bool geo_box_rotated_overlap_sphere(const GeoBoxRotated*, const GeoSphere*);
bool geo_box_rotated_overlap_frustum(const GeoBoxRotated*, const GeoVector frustum[8]);
