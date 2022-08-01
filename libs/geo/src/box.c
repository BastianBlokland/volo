#include "core_array.h"
#include "core_math.h"
#include "geo_box.h"

#define geo_box_simd_enable 1

#if geo_box_simd_enable
#include "simd_sse_internal.h"
#endif

GeoVector geo_box_center(const GeoBox* b) {
#if geo_box_simd_enable
  const SimdVec min  = simd_vec_load(b->min.comps);
  const SimdVec max  = simd_vec_load(b->max.comps);
  const SimdVec half = simd_vec_broadcast(0.5f);

  GeoVector res;
  simd_vec_store(simd_vec_mul(simd_vec_add(min, max), half), res.comps);
  return res;
#else
  return geo_vector_mul(geo_vector_add(b->min, b->max), .5f);
#endif
}

GeoVector geo_box_size(const GeoBox* b) {
#if geo_box_simd_enable
  const SimdVec min = simd_vec_load(b->min.comps);
  const SimdVec max = simd_vec_load(b->max.comps);
  GeoVector     res;
  simd_vec_store(simd_vec_sub(max, min), res.comps);
  return res;
#else
  return geo_vector_sub(b->max, b->min);
#endif
}

GeoVector geo_box_closest_point(const GeoBox* b, const GeoVector point) {
  return geo_vector(
      math_clamp_f32(point.x, b->min.x, b->max.x),
      math_clamp_f32(point.y, b->min.y, b->max.y),
      math_clamp_f32(point.z, b->min.z, b->max.z));
}

GeoBox geo_box_from_center(const GeoVector center, const GeoVector size) {
  const GeoVector halfSize = geo_vector_mul(size, 0.5f);
  return (GeoBox){
      .min = geo_vector_sub(center, halfSize),
      .max = geo_vector_add(center, halfSize),
  };
}

GeoBox geo_box_inverted2() {
  const GeoVector min = {f32_max, f32_max};
  const GeoVector max = {f32_min, f32_min};
  return (GeoBox){min, max};
}

GeoBox geo_box_inverted3() {
  const GeoVector min = {f32_max, f32_max, f32_max};
  const GeoVector max = {f32_min, f32_min, f32_min};
  return (GeoBox){min, max};
}

bool geo_box_is_inverted2(const GeoBox* b) { return b->min.x > b->max.x || b->min.y > b->max.y; }

bool geo_box_is_inverted3(const GeoBox* b) {
#if geo_box_simd_enable
  const SimdVec min = simd_vec_load(b->min.comps);
  const SimdVec max = simd_vec_load(b->max.comps);
  // NOTE: The non-simd impl doesn't take the w into account, is it worth ignoring it here also?
  return simd_vec_any_true(simd_vec_greater(min, max));
#else
  return b->min.x > b->max.x || b->min.y > b->max.y || b->min.z > b->max.z;
#endif
}

GeoBox geo_box_encapsulate2(const GeoBox* b, const GeoVector point) {
  GeoBox newBox = *b;
  for (usize i = 0; i != 2; ++i) {
    if (point.comps[i] < newBox.min.comps[i]) {
      newBox.min.comps[i] = point.comps[i];
    }
    if (point.comps[i] > newBox.max.comps[i]) {
      newBox.max.comps[i] = point.comps[i];
    }
  }
  return newBox;
}

GeoBox geo_box_encapsulate(const GeoBox* b, const GeoVector point) {
#if geo_box_simd_enable
  const SimdVec p   = simd_vec_load(point.comps);
  const SimdVec min = simd_vec_min(simd_vec_load(b->min.comps), p);
  const SimdVec max = simd_vec_max(simd_vec_load(b->max.comps), p);

  GeoBox newBox;
  simd_vec_store(min, newBox.min.comps);
  simd_vec_store(max, newBox.max.comps);
  return newBox;
#else
  return (GeoBox){
      .min = geo_vector_min(b->min, point),
      .max = geo_vector_max(b->max, point),
  };
#endif
}

void geo_box_corners3(const GeoBox* box, GeoVector corners[8]) {
  corners[0] = geo_vector(box->min.x, box->min.y, box->min.z);
  corners[1] = geo_vector(box->min.x, box->min.y, box->max.z);
  corners[2] = geo_vector(box->max.x, box->min.y, box->min.z);
  corners[3] = geo_vector(box->max.x, box->min.y, box->max.z);
  corners[4] = geo_vector(box->min.x, box->max.y, box->min.z);
  corners[5] = geo_vector(box->min.x, box->max.y, box->max.z);
  corners[6] = geo_vector(box->max.x, box->max.y, box->min.z);
  corners[7] = geo_vector(box->max.x, box->max.y, box->max.z);
}

GeoBox
geo_box_transform3(const GeoBox* box, const GeoVector pos, const GeoQuat rot, const f32 scale) {
#if geo_box_simd_enable
  SimdVec       min      = simd_vec_broadcast(f32_max);
  SimdVec       max      = simd_vec_broadcast(f32_min);
  const SimdVec posVec   = simd_vec_load(pos.comps);
  const SimdVec quatVec  = simd_vec_load(rot.comps);
  const SimdVec scaleVec = simd_vec_broadcast(scale);

  SimdVec points[8];
  points[0] = simd_vec_set(box->min.x, box->min.y, box->min.z, 0);
  points[1] = simd_vec_set(box->min.x, box->min.y, box->max.z, 0);
  points[2] = simd_vec_set(box->max.x, box->min.y, box->min.z, 0);
  points[3] = simd_vec_set(box->max.x, box->min.y, box->max.z, 0);
  points[4] = simd_vec_set(box->min.x, box->max.y, box->min.z, 0);
  points[5] = simd_vec_set(box->min.x, box->max.y, box->max.z, 0);
  points[6] = simd_vec_set(box->max.x, box->max.y, box->min.z, 0);
  points[7] = simd_vec_set(box->max.x, box->max.y, box->max.z, 0);

  for (usize i = 0; i != array_elems(points); ++i) {
    points[i] = simd_vec_mul(points[i], scaleVec);
    points[i] = simd_quat_rotate(quatVec, points[i]);
    points[i] = simd_vec_add(points[i], posVec);

    min = simd_vec_min(min, points[i]);
    max = simd_vec_max(max, points[i]);
  }

  GeoBox newBox;
  simd_vec_store(min, newBox.min.comps);
  simd_vec_store(max, newBox.max.comps);
  return newBox;
#else
  GeoVector points[8];
  geo_box_corners3(box, points);

  GeoBox newBox = geo_box_inverted3();
  for (usize i = 0; i != array_elems(points); ++i) {
    const GeoVector p = geo_vector_add(geo_quat_rotate(rot, geo_vector_mul(points[i], scale)), pos);
    newBox            = geo_box_encapsulate(&newBox, p);
  }
  return newBox;
#endif
}

GeoBox geo_box_from_sphere(const GeoVector pos, const f32 radius) {
#if geo_box_simd_enable
  const SimdVec vPos    = simd_vec_load(pos.comps);
  const SimdVec vRadius = simd_vec_clear_w(simd_vec_broadcast(radius));
  GeoBox        newBox;
  simd_vec_store(simd_vec_sub(vPos, vRadius), newBox.min.comps);
  simd_vec_store(simd_vec_add(vPos, vRadius), newBox.max.comps);
  return newBox;
#else
  return (GeoBox){
      .min = geo_vector(pos.x - radius, pos.y - radius, pos.z - radius),
      .max = geo_vector(pos.x + radius, pos.y + radius, pos.z + radius),
  };
#endif
}

GeoBox geo_box_from_capsule(const GeoVector a, const GeoVector b, const f32 radius) {
#if geo_box_simd_enable
  const SimdVec vRadius = simd_vec_clear_w(simd_vec_broadcast(radius));
  const SimdVec vA      = simd_vec_load(a.comps);
  const SimdVec vAMin   = simd_vec_sub(vA, vRadius);
  const SimdVec vAMax   = simd_vec_add(vA, vRadius);
  const SimdVec vB      = simd_vec_load(b.comps);
  const SimdVec vBMin   = simd_vec_sub(vB, vRadius);
  const SimdVec vBMax   = simd_vec_add(vB, vRadius);
  GeoBox        newBox;
  simd_vec_store(simd_vec_min(vAMin, vBMin), newBox.min.comps);
  simd_vec_store(simd_vec_max(vAMax, vBMax), newBox.max.comps);
  return newBox;
#else
  GeoBox box = geo_box_inverted3();
  box        = geo_box_encapsulate(&box, geo_vector(a.x - radius, a.y - radius, a.z - radius));
  box        = geo_box_encapsulate(&box, geo_vector(a.x + radius, a.y + radius, a.z + radius));
  box        = geo_box_encapsulate(&box, geo_vector(b.x - radius, b.y - radius, b.z - radius));
  box        = geo_box_encapsulate(&box, geo_vector(b.x + radius, b.y + radius, b.z + radius));
  return box;
#endif
}

GeoBox geo_box_from_cylinder(const GeoVector a, const GeoVector b, const f32 radius) {
#if geo_box_simd_enable
  const SimdVec vA        = simd_vec_load(a.comps);
  const SimdVec vB        = simd_vec_load(b.comps);
  const SimdVec toB       = simd_vec_sub(vB, vA);
  const SimdVec lengthSqr = simd_vec_dot3(toB, toB);
  const SimdVec dirSqr    = simd_vec_div(simd_vec_mul(toB, toB), lengthSqr);
  const SimdVec axisDir   = simd_vec_sqrt(simd_vec_sub(simd_vec_broadcast(1.0f), dirSqr));
  const SimdVec axisDelta = simd_vec_clear_w(simd_vec_mul(axisDir, simd_vec_broadcast(radius)));
  GeoBox        res;
  simd_vec_store(
      simd_vec_min(simd_vec_sub(vA, axisDelta), simd_vec_sub(vB, axisDelta)), res.min.comps);
  simd_vec_store(
      simd_vec_max(simd_vec_add(vA, axisDelta), simd_vec_add(vB, axisDelta)), res.max.comps);
  return res;
#else
  const GeoVector toB       = geo_vector_sub(b, a);
  const f32       lengthSqr = geo_vector_mag_sqr(toB);
  const GeoVector dirSqr    = geo_vector_div(geo_vector_mul_comps(toB, toB), lengthSqr);
  const GeoVector axisDir   = geo_vector_sqrt(geo_vector_sub(geo_vector(1, 1, 1), dirSqr));
  const GeoVector axisDelta = geo_vector_mul(axisDir, radius);

  return (GeoBox){
      .min = geo_vector_min(geo_vector_sub(a, axisDelta), geo_vector_sub(b, axisDelta)),
      .max = geo_vector_max(geo_vector_add(a, axisDelta), geo_vector_add(b, axisDelta)),
  };
#endif
}

GeoBox geo_box_from_cone(const GeoVector bottom, const GeoVector top, const f32 radius) {
#if geo_box_simd_enable
  const SimdVec vBottom   = simd_vec_load(bottom.comps);
  const SimdVec vTop      = simd_vec_load(top.comps);
  const SimdVec toTop     = simd_vec_sub(vTop, vBottom);
  const SimdVec lengthSqr = simd_vec_dot3(toTop, toTop);
  const SimdVec dirSqr    = simd_vec_div(simd_vec_mul(toTop, toTop), lengthSqr);
  const SimdVec axisDir   = simd_vec_sqrt(simd_vec_sub(simd_vec_broadcast(1.0f), dirSqr));
  const SimdVec axisDelta = simd_vec_clear_w(simd_vec_mul(axisDir, simd_vec_broadcast(radius)));
  GeoBox        res;
  simd_vec_store(simd_vec_min(simd_vec_sub(vBottom, axisDelta), vTop), res.min.comps);
  simd_vec_store(simd_vec_max(simd_vec_add(vBottom, axisDelta), vTop), res.max.comps);
  return res;
#else
  const GeoVector toTop     = geo_vector_sub(top, bottom);
  const f32       lengthSqr = geo_vector_mag_sqr(toTop);
  const GeoVector dirSqr    = geo_vector_div(geo_vector_mul_comps(toTop, toTop), lengthSqr);
  const GeoVector axisDir   = geo_vector_sqrt(geo_vector_sub(geo_vector(1, 1, 1), dirSqr));
  const GeoVector axisDelta = geo_vector_mul(axisDir, radius);

  return (GeoBox){
      .min = geo_vector_min(geo_vector_sub(bottom, axisDelta), top),
      .max = geo_vector_max(geo_vector_add(bottom, axisDelta), top),
  };
#endif
}

GeoBox geo_box_from_line(const GeoVector from, const GeoVector to) {
  return (GeoBox){
      .min = geo_vector_min(from, to),
      .max = geo_vector_max(from, to),
  };
}

bool geo_box_intersect_frustum4(const GeoBox* box, const GeoPlane frustum[4]) {
#if geo_box_simd_enable
  const SimdVec boxMin = simd_vec_load(box->min.comps);
  const SimdVec boxMax = simd_vec_load(box->max.comps);
  if (simd_vec_any_true(simd_vec_greater(boxMin, boxMax))) {
    return true; // Box is inverted.
  }
  for (usize i = 0; i != 4; ++i) {
    const SimdVec planeNorm           = simd_vec_load(frustum[i].normal.comps);
    const SimdVec greaterThenZeroMask = simd_vec_greater(planeNorm, simd_vec_zero());
    const SimdVec max                 = simd_vec_select(boxMin, boxMax, greaterThenZeroMask);
    const SimdVec dot                 = simd_vec_dot4(planeNorm, max);
    if (-simd_vec_x(dot) > frustum[i].distance) {
      return false;
    }
  }
  return true;
#else
  if (geo_box_is_inverted3(box)) {
    return true;
  }
  for (usize i = 0; i != 4; ++i) {
    const GeoVector max = {
        .x = frustum[i].normal.x > 0 ? box->max.x : box->min.x,
        .y = frustum[i].normal.y > 0 ? box->max.y : box->min.y,
        .z = frustum[i].normal.z > 0 ? box->max.z : box->min.z,
    };
    if (-geo_vector_dot(frustum[i].normal, max) > frustum[i].distance) {
      return false;
    }
  }
  return true;
#endif
}

f32 geo_box_intersect_ray(const GeoBox* box, const GeoRay* ray, GeoVector* outNormal) {
  /**
   * Find the intersection of the axis-aligned box the given ray using Cyrus-Beck clipping.
   * More information: https://izzofinal.wordpress.com/2012/11/09/ray-vs-box-round-1/
   */

  const f32 t1 = (box->min.x - ray->point.x) / ray->dir.x;
  const f32 t2 = (box->max.x - ray->point.x) / ray->dir.x;
  const f32 t3 = (box->min.y - ray->point.y) / ray->dir.y;
  const f32 t4 = (box->max.y - ray->point.y) / ray->dir.y;
  const f32 t5 = (box->min.z - ray->point.z) / ray->dir.z;
  const f32 t6 = (box->max.z - ray->point.z) / ray->dir.z;

  const f32 minA = math_min(t1, t2);
  const f32 minB = math_min(t3, t4);
  const f32 minC = math_min(t5, t6);
  const f32 tMin = math_max(math_max(minA, minB), minC);

  const f32 maxA = math_max(t1, t2);
  const f32 maxB = math_max(t3, t4);
  const f32 maxC = math_max(t5, t6);
  const f32 tMax = math_min(math_min(maxA, maxB), maxC);

  // if tMax < 0: ray intersects the box, but whole box is behind us.
  if (tMax < 0) {
    return -1.0f;
  }

  // if tMin > tMax: ray misses the box.
  if (tMin > tMax) {
    return -1.0f;
  }

  const f32 result = tMin >= 0 ? tMin : tMax;

  // Calculate the surface normal.
  if (minA >= minB && minA >= minC) { // A is the biggest meaning we're on the X plane
    *outNormal = t1 <= t2 ? geo_left : geo_right;
  } else if (minB >= minA && minB >= minC) { // B is the biggest meaning we're on the Y plane
    *outNormal = t3 <= t4 ? geo_down : geo_up;
  } else { // C is the biggest meaning we're on the Z plane
    *outNormal = t5 <= t6 ? geo_backward : geo_forward;
  }

  return result;
}
