#include "core_math.h"
#include "geo_ray.h"

GeoVector geo_ray_position(const GeoRay* ray, const f32 time) {
  return geo_vector_add(ray->point, geo_vector_mul(ray->dir, time));
}
