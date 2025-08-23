#pragma once
#include "geo/forward.h"
#include "geo/vector.h"

/**
 * Line in 3 dimensional space.
 */

typedef struct sGeoLine {
  GeoVector a, b;
} GeoLine;

/**
 * Transform the given line.
 */
GeoLine geo_line_transform3(const GeoLine*, GeoVector offset, GeoQuat rotation, f32 scale);

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
 * Find the time on the line (0 - 1) that is the closest to the given point / ray.
 */
f32 geo_line_closest_time(const GeoLine*, GeoVector point);
f32 geo_line_closest_time_ray(const GeoLine*, const GeoRay* ray);

/**
 * Find the closest position on the line to the given point / ray.
 */
GeoVector geo_line_closest_point(const GeoLine*, GeoVector point);
GeoVector geo_line_closest_point_ray(const GeoLine*, const GeoRay* ray);

/**
 * Compute the distance squared from the line to the given point.
 */
f32 geo_line_distance_sqr_point(const GeoLine*, GeoVector point);
