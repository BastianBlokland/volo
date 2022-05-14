#include "core_array.h"
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
  return b->min.x > b->max.x || b->min.y > b->max.y || b->min.z > b->max.z;
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

GeoBox geo_box_encapsulate3(const GeoBox* b, const GeoVector point) {
#if geo_box_simd_enable
  const SimdVec p   = simd_vec_load(point.comps);
  const SimdVec min = simd_vec_min(simd_vec_load(b->min.comps), p);
  const SimdVec max = simd_vec_max(simd_vec_load(b->max.comps), p);

  GeoBox newBox;
  simd_vec_store(min, newBox.min.comps);
  simd_vec_store(max, newBox.max.comps);

  // NOTE: Preserve the w values to match the non-simd implementation.
  newBox.min.w = b->min.w;
  newBox.max.w = b->max.w;
  return newBox;
#else
  GeoBox newBox = *b;
  for (usize i = 0; i != 3; ++i) {
    if (point.comps[i] < newBox.min.comps[i]) {
      newBox.min.comps[i] = point.comps[i];
    }
    if (point.comps[i] > newBox.max.comps[i]) {
      newBox.max.comps[i] = point.comps[i];
    }
  }
  return newBox;
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

  GeoVector points[8];
  geo_box_corners3(box, points);

  for (usize i = 0; i != array_elems(points); ++i) {
    SimdVec point = simd_vec_load(points[i].comps);
    point         = simd_vec_mul(point, scaleVec);
    point         = simd_quat_rotate(quatVec, point);
    point         = simd_vec_add(point, posVec);

    min = simd_vec_min(min, point);
    max = simd_vec_max(max, point);
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
    newBox            = geo_box_encapsulate3(&newBox, p);
  }
  return newBox;
#endif
}
