#pragma once
#include "geo_quat.h"
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

/**
 * Retrieve the 8 cornsers of the 3d box.
 */
void geo_box_corners3(const GeoBox*, GeoVector corners[8]);

/**
 * Construct a transformed box.
 * NOTE: The resulting box is still axis aligned so can be substantially larger then the original.
 */
GeoBox geo_box_transform(const GeoBox*, GeoVector offset, GeoQuat rotation, f32 scale);
