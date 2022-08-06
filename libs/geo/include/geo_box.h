#pragma once
#include "geo_plane.h"
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
 * Retrieve the 8 corners of the 3d box.
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
 * Test if the box intersects the given four frustum planes.
 * NOTE: If the given box is inverted its considered to always be intersecting.
 */
bool geo_box_intersect_frustum4(const GeoBox*, const GeoPlane frustum[4]);

/**
 * Compute the intersection of the box with the given ray.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 * NOTE: Writes the surface-normal at the point of intersection to 'outNormal'.
 */
f32 geo_box_intersect_ray(const GeoBox*, const GeoRay*, GeoVector* outNormal);
