#include "core_diag.h"
#include "core_math.h"
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

f32 geo_line_closest_time(const GeoLine* line, const GeoVector point) {
  const GeoVector toB = geo_vector_sub(line->b, line->a);
  const f32 t = geo_vector_dot(geo_vector_sub(point, line->a), toB) / geo_vector_dot(toB, toB);
  return math_clamp_f32(t, 0, 1);
}

GeoVector geo_line_closest_point(const GeoLine* line, const GeoVector point) {
  const f32 t = geo_line_closest_time(line, point);
  return geo_vector_lerp(line->a, line->b, t);
}
