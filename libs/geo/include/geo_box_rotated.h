#pragma once
#include "geo_box.h"
#include "geo_quat.h"

/**
 * Geometric 3d rotated box.
 */

typedef struct {
  GeoBox  box;
  GeoQuat rotation;
} GeoBoxRotated;
