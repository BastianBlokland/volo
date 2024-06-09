#pragma once
#include "geo_box.h"
#include "geo_quat.h"

// Forward declare from 'geo_sphere.h'.
typedef struct sGeoSphere GeoSphere;

/**
 * Geometric 3d rotated box.
 */

typedef struct sGeoBoxRotated {
  GeoBox  box;
  GeoQuat rotation;
} GeoBoxRotated;

/**
 * Construct a rotated box from a local box and a transformation.
 */
GeoBoxRotated geo_box_rotated(const GeoBox*, GeoVector pos, GeoQuat rot, f32 scale);

/**
 * Dilate the box by the given amount on all sides.
 */
GeoBoxRotated geo_box_rotated_dilate(const GeoBoxRotated*, GeoVector size);

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
f32 geo_box_rotated_intersect_ray(const GeoBoxRotated*, const GeoRay*);
f32 geo_box_rotated_intersect_ray_info(const GeoBoxRotated*, const GeoRay*, GeoVector* outNormal);

/**
 * Get the closest point within the rotated box to the given point.
 */
GeoVector geo_box_rotated_closest_point(const GeoBoxRotated*, GeoVector point);

/**
 * Overlap tests.
 */
bool geo_box_rotated_overlap_box(const GeoBoxRotated*, const GeoBox*);
bool geo_box_rotated_overlap_box_rotated(const GeoBoxRotated*, const GeoBoxRotated*);
bool geo_box_rotated_overlap_sphere(const GeoBoxRotated*, const GeoSphere*);
bool geo_box_rotated_overlap_frustum(const GeoBoxRotated*, const GeoVector[PARAM_ARRAY_SIZE(8)]);
