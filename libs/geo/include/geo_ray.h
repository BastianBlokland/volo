#pragma once
#include "geo_vector.h"

/**
 * Ray in 3 dimensional space.
 */

typedef struct {
  GeoVector point;
  GeoVector direction; // Normalized.
} GeoRay;
