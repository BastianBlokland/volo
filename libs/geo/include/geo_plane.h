#pragma once
#include "geo_vector.h"

/**
 * Geometric 3d plane.
 */

typedef struct {
  GeoVector normal; // NOTE: Unit vector.
  f32       distance;
} GeoPlane;

/**
 * Construct a plane at the given position.
 */
GeoPlane geo_plane_at(GeoVector normal, GeoVector position);
