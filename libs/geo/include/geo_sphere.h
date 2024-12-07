#pragma once
#include "geo_box.h"
#include "geo_ray.h"
#include "geo_vector.h"

/**
 * Sphere in 3 dimensional space.
 */

typedef struct sGeoSphere {
  GeoVector point;
  f32       radius;
} GeoSphere;

/**
 * Dilate the sphere by the given amount.
 */
GeoSphere geo_sphere_dilate(const GeoSphere*, f32 radius);

/**
 * Transform the given sphere.
 */
GeoSphere geo_sphere_transform3(const GeoSphere*, GeoVector offset, GeoQuat rotation, f32 scale);

/**
 * Compute the intersection of the sphere with the given ray.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 * NOTE: Writes the surface-normal at the point of intersection to 'outNormal'.
 */
f32 geo_sphere_intersect_ray(const GeoSphere*, const GeoRay*);
f32 geo_sphere_intersect_ray_info(const GeoSphere*, const GeoRay*, GeoVector* outNormal);

/**
 * Overlap tests.
 */
bool geo_sphere_overlap(const GeoSphere*, const GeoSphere*);
bool geo_sphere_overlap_box(const GeoSphere*, const GeoBox*);
bool geo_sphere_overlap_frustum(const GeoSphere*, const GeoVector[PARAM_ARRAY_SIZE(8)]);
