#pragma once
#include "geo/forward.h"
#include "geo/plane.h"
#include "geo/vector.h"

/**
 * Geometric 3d axis-aligned box.
 */

typedef struct sGeoBox {
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
 * Get the closest point within the box to the given point.
 */
GeoVector geo_box_closest_point(const GeoBox*, GeoVector point);

/**
 * Construct a box from a center and a size.
 */
GeoBox geo_box_from_center(GeoVector center, GeoVector size);

/**
 * Construct an 'Inside out' (infinitely small) box.
 * Useful as a starting point for encapsulating points.
 */
GeoBox geo_box_inverted2(void);
GeoBox geo_box_inverted3(void);

/**
 * Check if the given box is inverted.
 */
bool geo_box_is_inverted2(const GeoBox*);
bool geo_box_is_inverted3(const GeoBox*);

/**
 * Compute a new box that encapsulates the existing box and the new point / box.
 */
GeoBox geo_box_encapsulate2(const GeoBox*, GeoVector point);
GeoBox geo_box_encapsulate(const GeoBox*, GeoVector point);
GeoBox geo_box_encapsulate_box(const GeoBox*, const GeoBox*);

/**
 * Dilate the box by the given amount on all sides.
 */
GeoBox geo_box_dilate(const GeoBox*, GeoVector size);

/**
 * Retrieve the 8 corners of the 3d box.
 */
void geo_box_corners3(const GeoBox*, GeoVector corners[PARAM_ARRAY_SIZE(8)]);

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
 * Calculate the bounding box of a rotated box.
 * NOTE: Rotation is applied around the box's center.
 */
GeoBox geo_box_from_rotated(const GeoBox*, GeoQuat rotation);

/**
 * Calculate the bounding box of a Capsule.
 */
GeoBox geo_box_from_capsule(GeoVector bottom, GeoVector top, f32 radius);

/**
 * Calculate the bounding box of a cylinder.
 */
GeoBox geo_box_from_cylinder(GeoVector bottom, GeoVector top, f32 radius);

/**
 * Calculate the bounding box of a cone.
 */
GeoBox geo_box_from_cone(GeoVector bottom, GeoVector top, f32 radius);

/**
 * Calculate the bounding box of a line.
 */
GeoBox geo_box_from_line(GeoVector from, GeoVector to);

/**
 * Calculate the bounding box of quad.
 */
GeoBox geo_box_from_quad(GeoVector center, f32 sizeX, f32 sizeY, GeoQuat rotation);

/**
 * Calculate the bounding box of the frustum formed by the given 8 corners.
 * NOTE: Defines the frustum by its corner points.
 */
GeoBox geo_box_from_frustum(const GeoVector[PARAM_ARRAY_SIZE(8)]);

/**
 * Test if the given point is contained in the box.
 */
bool geo_box_contains3(const GeoBox*, GeoVector point);

/**
 * Compute the intersection of the box with the given ray.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 * NOTE: Writes the surface-normal at the point of intersection to 'outNormal'.
 */
f32 geo_box_intersect_ray(const GeoBox*, const GeoRay*);
f32 geo_box_intersect_ray_info(const GeoBox*, const GeoRay*, GeoVector* outNormal);

/**
 * Overlap tests.
 */
bool geo_box_overlap(const GeoBox*, const GeoBox*);
bool geo_box_overlap_sphere(const GeoBox*, const GeoSphere*);

/**
 * Test if the box overlaps a partial frustum given by four side planes.
 * Conservative approximation, false positives are possible but false negatives are not.
 * NOTE: If the given box is inverted its considered to always be overlapping.
 * NOTE: Defines a partial frustum by its four side planes.
 */
bool geo_box_overlap_frustum4_approx(const GeoBox*, const GeoPlane frustum[4]);
