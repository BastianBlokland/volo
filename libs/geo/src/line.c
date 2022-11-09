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
  if (length <= f32_epsilon) {
    return geo_forward; // Zero length line.
  }
  return geo_vector_div(delta, length);
}

f32 geo_line_closest_time(const GeoLine* line, const GeoVector point) {
  const GeoVector toB       = geo_vector_sub(line->b, line->a);
  const f32       lengthSqr = geo_vector_dot(toB, toB);
  if (lengthSqr < f32_epsilon) {
    return 0; // Zero length line.
  }
  const f32 t = geo_vector_dot(geo_vector_sub(point, line->a), toB) / lengthSqr;
  return math_clamp_f32(t, 0, 1);
}

f32 geo_line_closest_time_ray(const GeoLine* line, const GeoRay* ray) {
  const GeoVector lineDir = geo_line_direction(line);
  const f32       dot     = geo_vector_dot(lineDir, ray->dir);
  const f32       d       = 1.0f - (dot * dot);

  if (d != 0) {
    const GeoVector toA  = geo_vector_sub(line->a, ray->point);
    const f32       c    = geo_vector_dot(lineDir, toA);
    const f32       f    = geo_vector_dot(ray->dir, toA);
    const f32       dist = ((dot * f) - c) / d;
    if (dist <= 0) {
      return 0;
    }
    const f32 lineLength = geo_line_length(line);
    if (dist >= lineLength) {
      return 1;
    }
    return dist / lineLength;
  }

  return 0; // Line is parallel to the ray.
}

GeoVector geo_line_closest_point(const GeoLine* line, const GeoVector point) {
  const f32 t = geo_line_closest_time(line, point);
  return geo_vector_lerp(line->a, line->b, t);
}

GeoVector geo_line_closest_point_ray(const GeoLine* line, const GeoRay* ray) {
  const f32 t = geo_line_closest_time_ray(line, ray);
  return geo_vector_lerp(line->a, line->b, t);
}

f32 geo_line_distance_sqr_point(const GeoLine* line, const GeoVector point) {
  const GeoVector pointOnLine = geo_line_closest_point(line, point);
  return geo_vector_mag_sqr(geo_vector_sub(point, pointOnLine));
}
