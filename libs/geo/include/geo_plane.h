#pragma once
#include "geo_ray.h"
#include "geo_vector.h"

/**
 * Geometric 3d plane.
 */

typedef struct sGeoPlane {
  GeoVector normal; // NOTE: Unit vector.
  f32       distance;
} GeoPlane;

/**
 * Construct a plane at the given position.
 */
GeoPlane geo_plane_at(GeoVector normal, GeoVector position);
GeoPlane geo_plane_at_triangle(GeoVector a, GeoVector b, GeoVector c);

/**
 * Get a position on the plane's surface.
 */
GeoVector geo_plane_position(const GeoPlane*);

/**
 * Get the closest point on the given plane.
 */
GeoVector geo_plane_closest_point(const GeoPlane*, GeoVector point);

/**
 * Compute the intersection of the plane with the given ray.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 */
f32 geo_plane_intersect_ray(const GeoPlane*, const GeoRay*);
