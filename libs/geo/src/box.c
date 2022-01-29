#include "geo_box.h"

GeoVector geo_box_center(const GeoBox* b) {
  return geo_vector_mul(geo_vector_add(b->min, b->max), .5f);
}

GeoVector geo_box_size(const GeoBox* b) { return geo_vector_sub(b->max, b->min); }

GeoBox geo_box_inverted() {
  const GeoVector min = {f32_max, f32_max, f32_max};
  const GeoVector max = {f32_min, f32_min, f32_min};
  return (GeoBox){min, max};
}

GeoBox geo_box_encapsulate(const GeoBox* b, const GeoVector point) {
  GeoBox newBox = *b;
  for (usize i = 0; i != 4; ++i) {
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
