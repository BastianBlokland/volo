#pragma once
#include "geo_ray.h"
#include "geo_vector.h"

/**
 * Sphere in 3 dimensional space.
 */

typedef struct {
  GeoVector point;
  f32       radius;
} GeoSphere;

/**
 * Compute the intersection of the sphere with the given ray.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 */
f32 geo_sphere_intersect_ray(const GeoSphere*, const GeoRay*);
