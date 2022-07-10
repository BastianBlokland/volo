#pragma once
#include "geo_vector.h"

/**
 * Ray in 3 dimensional space.
 */

typedef struct {
  GeoVector point;
  GeoVector dir; // Normalized.
} GeoRay;

/**
 * Compute the position along the ray at the given time.
 */
GeoVector geo_ray_position(const GeoRay*, f32 time);
