#include "core_diag.h"
#include "geo_line.h"

f32 geo_line_length(const GeoLine* line) {
  const GeoVector delta = geo_vector_sub(line->b, line->a);
  return geo_vector_mag(delta);
}

f32 geo_line_length_sqr(const GeoLine* line) {
  const GeoVector delta = geo_vector_sub(line->b, line->a);
  return geo_vector_mag_sqr(delta);
}

GeoVector geo_line_direction(const GeoLine* line) {
  const GeoVector delta  = geo_vector_sub(line->b, line->a);
  const f32       length = geo_vector_mag(delta);
  diag_assert(length != 0);
  return geo_vector_div(delta, length);
}
