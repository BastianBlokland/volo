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

/**
 * Find the time on the line (0 - 1) that is the closest to the given point.
 */
f32 geo_line_closest_time(const GeoLine*, GeoVector point);

/**
 * Find the closest position on the line to the given point.
 */
GeoVector geo_line_closest_point(const GeoLine*, GeoVector point);
