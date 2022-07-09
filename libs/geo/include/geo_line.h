#pragma once
#include "geo_vector.h"

/**
 * Line in 3 dimensional space.
 */

typedef struct {
  GeoVector from, to;
} GeoLine;
