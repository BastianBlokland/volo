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

/**
 * Compute the direction of the line.
 * Pre-condition: geo_line_length(line) > 0
 */
GeoVector geo_line_direction(const GeoLine*);
