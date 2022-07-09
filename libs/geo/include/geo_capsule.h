#pragma once
#include "geo_line.h"

/**
 * Capsule in 3 dimensional space.
 */

typedef struct {
  GeoLine line;
  f32     radius;
} GeoCapsule;
