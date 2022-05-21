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
GeoBox geo_box_inverted2();
GeoBox geo_box_inverted3();

/**
 * Check if the given box is inverted.
 */
bool geo_box_is_inverted2(const GeoBox*);
bool geo_box_is_inverted3(const GeoBox*);

/**
 * Compute a new box that encapsulates the existing box and the new point.
 */
GeoBox geo_box_encapsulate2(const GeoBox*, GeoVector point);
GeoBox geo_box_encapsulate(const GeoBox*, GeoVector point);

/**
 * Retrieve the 8 cornsers of the 3d box.
 */
void geo_box_corners3(const GeoBox*, GeoVector corners[8]);

/**
 * Construct a transformed 3d box.
 * NOTE: The resulting box is still axis aligned so can be substantially larger then the original.
 */
GeoBox geo_box_transform3(const GeoBox*, GeoVector offset, GeoQuat rotation, f32 scale);

/**
 * Calculate the bounding box of a sphere.
 */
GeoBox geo_box_from_sphere(GeoVector, f32 radius);

/**
 * Calculate the bounding box of a cone.
 */
GeoBox geo_box_from_cone(GeoVector bottom, GeoVector top, f32 radius);
