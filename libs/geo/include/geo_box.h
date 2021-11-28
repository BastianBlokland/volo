#pragma once
#include "geo_vector.h"

/**
 * Geometric 3d axis-aligned box.
 */

typedef struct {
  GeoVector min, max;
} GeoBox;

/**
 * Return the center point of the given box.
 */
GeoVector geo_box_center(const GeoBox*);

/**
 * Return the size of the given box.
 */
GeoVector geo_box_size(const GeoBox*);

/**
 * Construct an 'Inside out' (infinitely small) box.
 * Usefull as a starting point for encapsulating points.
 */
GeoBox geo_box_inverted();

/**
 * Compute a new box that encapsulates the existing box and the new point.
 */
GeoBox geo_box_encapsulate(const GeoBox*, GeoVector point);
