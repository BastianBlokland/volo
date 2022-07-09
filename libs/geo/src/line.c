#include "geo_line.h"

f32 geo_line_length(const GeoLine* line) {
  const GeoVector delta = geo_vector_sub(line->a, line->b);
  return geo_vector_mag(delta);
}

f32 geo_line_length_sqr(const GeoLine* line) {
  const GeoVector delta = geo_vector_sub(line->a, line->b);
  return geo_vector_mag_sqr(delta);
}
