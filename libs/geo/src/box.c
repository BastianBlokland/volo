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
