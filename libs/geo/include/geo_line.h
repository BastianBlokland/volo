#pragma once
#include "geo_vector.h"

/**
 * Line in 3 dimensional space.
 */

typedef struct {
  GeoVector a, b;
} GeoLine;

/**
 * Compute the length of the line.
 */
f32 geo_line_length(const GeoLine*);
f32 geo_line_length_sqr(const GeoLine*);
