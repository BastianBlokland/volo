#include "core_array.h"
#include "geo_box.h"

GeoVector geo_box_center(const GeoBox* b) {
  return geo_vector_mul(geo_vector_add(b->min, b->max), .5f);
}

GeoVector geo_box_size(const GeoBox* b) { return geo_vector_sub(b->max, b->min); }

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
  GeoVector points[8];
  geo_box_corners3(box, points);

  GeoBox newBox = geo_box_inverted3();
  for (usize i = 0; i != array_elems(points); ++i) {
    const GeoVector p = geo_vector_add(geo_quat_rotate(rot, geo_vector_mul(points[i], scale)), pos);
    newBox            = geo_box_encapsulate3(&newBox, p);
  }
  return newBox;
}
