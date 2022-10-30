#include "core_array.h"
#include "core_float.h"
#include "core_math.h"
#include "geo_box.h"
#include "geo_box_rotated.h"

/**
 * Separating Axis Theorem helpers to check if two sets of points overlap on a given axis.
 */

static bool geo_sat_in_range(const f32 val, const f32 min, const f32 max) {
  return val >= min && val <= max;
}

static bool geo_sat_overlapping1(const f32 min1, const f32 max1, const f32 min2, const f32 max2) {
  return geo_sat_in_range(min2, min1, max1) || geo_sat_in_range(min1, min2, max2);
}

static void
geo_sat_proj3(const GeoVector axis, const GeoVector points[8], f32* outMin, f32* outMax) {
  *outMin = f32_max;
  *outMax = f32_min;

  for (usize i = 0; i != 8; ++i) {
    const f32 dist = geo_vector_dot(points[i], axis);
    if (dist < *outMin) {
      *outMin = dist;
    }
    if (dist > *outMax) {
      *outMax = dist;
    }
  }
}

static bool geo_sat_overlapping3(const GeoVector axis, const GeoVector a[8], const GeoVector b[8]) {
  f32 minDistA, maxDistA, minDistB, maxDistB;
  geo_sat_proj3(axis, a, &minDistA, &maxDistA);
  geo_sat_proj3(axis, b, &minDistB, &maxDistB);
  return geo_sat_overlapping1(minDistA, maxDistA, minDistB, maxDistB);
}

static GeoVector geo_rotate_around(const GeoVector point, const GeoQuat rot, const GeoVector v) {
  return geo_vector_add(point, geo_quat_rotate(rot, geo_vector_sub(v, point)));
}

static void geo_box_corners(const GeoBox* b, GeoVector out[8]) {
  out[0] = geo_vector(b->min.x, b->min.y, b->min.z);
  out[1] = geo_vector(b->min.x, b->min.y, b->max.z);
  out[2] = geo_vector(b->max.x, b->min.y, b->min.z);
  out[3] = geo_vector(b->max.x, b->min.y, b->max.z);
  out[4] = geo_vector(b->min.x, b->max.y, b->min.z);
  out[5] = geo_vector(b->min.x, b->max.y, b->max.z);
  out[6] = geo_vector(b->max.x, b->max.y, b->min.z);
  out[7] = geo_vector(b->max.x, b->max.y, b->max.z);
}

static void geo_box_rotated_corners(const GeoBoxRotated* b, GeoVector out[8]) {
  const GeoVector c = geo_box_center(&b->box);
  geo_box_corners(&b->box, out);
  for (u32 i = 0; i != 8; ++i) {
    out[i] = geo_rotate_around(c, b->rotation, out[i]);
  }
}

GeoBoxRotated
geo_box_rotated_from_capsule(const GeoVector bottom, const GeoVector top, const f32 radius) {
  const GeoVector toTop  = geo_vector_sub(top, bottom);
  const f32       height = geo_vector_mag(toTop);
  if (height <= f32_epsilon) {
    return (GeoBoxRotated){.box = geo_box_from_sphere(bottom, radius), .rotation = geo_quat_ident};
  }
  const GeoVector center      = geo_vector_add(bottom, geo_vector_mul(toTop, 0.5f));
  const GeoVector localExtent = geo_vector_mul(geo_forward, height * 0.5f);
  const GeoVector localBottom = geo_vector_sub(center, localExtent);
  const GeoVector localTop    = geo_vector_add(center, localExtent);
  const GeoVector dir         = geo_vector_div(toTop, height);
  return (GeoBoxRotated){
      .box      = geo_box_from_capsule(localBottom, localTop, radius),
      .rotation = geo_quat_look(dir, geo_up),
  };
}

static GeoRay geo_box_rotated_local_ray(const GeoBoxRotated* box, const GeoRay* worldRay) {
  const GeoVector boxCenter      = geo_box_center(&box->box);
  const GeoQuat   boxInvRotation = geo_quat_inverse(box->rotation);
  return (GeoRay){
      .point = geo_rotate_around(boxCenter, boxInvRotation, worldRay->point),
      .dir   = geo_quat_rotate(boxInvRotation, worldRay->dir),
  };
}

f32 geo_box_rotated_intersect_ray(
    const GeoBoxRotated* box, const GeoRay* ray, GeoVector* outNormal) {
  /**
   * Transform the ray to the local space of the box and perform the intersection on the non-rotated
   * box.
   */

  const GeoRay localRay = geo_box_rotated_local_ray(box, ray);
  const f32    rayHitT  = geo_box_intersect_ray(&box->box, &localRay, outNormal);
  if (rayHitT >= 0.0f) {
    /**
     * Transform the surface normal back to world-space.
     */
    *outNormal = geo_quat_rotate(box->rotation, *outNormal);
  }

  return rayHitT;
}

bool geo_box_rotated_intersect_frustum(const GeoBoxRotated* box, const GeoVector frustum[8]) {
  GeoVector boxPoints[8];
  geo_box_rotated_corners(box, boxPoints);

  const GeoVector boxAxes[] = {
      geo_quat_rotate(box->rotation, geo_right),
      geo_quat_rotate(box->rotation, geo_up),
      geo_quat_rotate(box->rotation, geo_forward),
  };
  const GeoVector frustumAxes[] = {
      geo_plane_at_triangle(frustum[2], frustum[6], frustum[3]).normal, // Right.
      geo_plane_at_triangle(frustum[0], frustum[4], frustum[1]).normal, // Left.
      geo_plane_at_triangle(frustum[1], frustum[5], frustum[2]).normal, // Up.
      geo_plane_at_triangle(frustum[0], frustum[3], frustum[4]).normal, // Down.
      geo_plane_at_triangle(frustum[6], frustum[5], frustum[4]).normal, // Forward.
  };
  const GeoVector frustumEdges[] = {
      frustumAxes[0], // Right.
      frustumAxes[2], // Up.
      geo_vector_sub(frustum[4], frustum[0]),
      geo_vector_sub(frustum[5], frustum[1]),
      geo_vector_sub(frustum[6], frustum[2]),
      geo_vector_sub(frustum[7], frustum[3]),
  };

  // Check the axes of the box.
  array_for_t(boxAxes, GeoVector, boxAxis) {
    if (!geo_sat_overlapping3(*boxAxis, boxPoints, frustum)) {
      return false;
    }
  }

  // Check the axes of the frustum.
  array_for_t(frustumAxes, GeoVector, frustumAxis) {
    if (!geo_sat_overlapping3(*frustumAxis, boxPoints, frustum)) {
      return false;
    }
  }

  // Check the derived axes (cross of all the edges).
  array_for_t(boxAxes, GeoVector, boxAxis) {
    array_for_t(frustumEdges, GeoVector, frustumEdge) {
      if (!geo_sat_overlapping3(geo_vector_cross3(*boxAxis, *frustumEdge), boxPoints, frustum)) {
        return false;
      }
    }
  }

  return true; // No separating axis found; boxes are overlapping.
}

bool geo_box_rotated_overlap_box(const GeoBoxRotated* a, const GeoBox* b) {
  /**
   * Check if two boxes are overlapping using the Separating Axis Theorem:
   * If there is any axis where they are not overlapping (in 1 dimension) then they are not
   * overlapping at all.
   */

  GeoVector pointsA[8], pointsB[8];
  geo_box_rotated_corners(a, pointsA);
  geo_box_corners(b, pointsB);

  const GeoVector axesA[] = {
      geo_quat_rotate(a->rotation, geo_right),
      geo_quat_rotate(a->rotation, geo_up),
      geo_quat_rotate(a->rotation, geo_forward),
  };
  const GeoVector axesB[] = {geo_right, geo_up, geo_forward};

  // Check the world axes of b.
  f32 minDist, maxDist;
  geo_sat_proj3(geo_up, pointsA, &minDist, &maxDist);
  if (!geo_sat_overlapping1(minDist, maxDist, b->min.y, b->max.y)) {
    return false;
  }
  geo_sat_proj3(geo_forward, pointsA, &minDist, &maxDist);
  if (!geo_sat_overlapping1(minDist, maxDist, b->min.z, b->max.z)) {
    return false;
  }
  geo_sat_proj3(geo_right, pointsA, &minDist, &maxDist);
  if (!geo_sat_overlapping1(minDist, maxDist, b->min.x, b->max.x)) {
    return false;
  }

  // Check the local axes of a.
  array_for_t(axesA, GeoVector, axisA) {
    if (!geo_sat_overlapping3(*axisA, pointsA, pointsB)) {
      return false;
    }
  }

  // Check the derived axes.
  array_for_t(axesA, GeoVector, axisA) {
    array_for_t(axesB, GeoVector, axisB) {
      if (!geo_sat_overlapping3(geo_vector_cross3(*axisA, *axisB), pointsA, pointsB)) {
        return false;
      }
    }
  }

  return true; // No separating axis found; boxes are overlapping.
}
