#include "core_math.h"
#include "geo_capsule.h"
#include "geo_sphere.h"

f32 geo_capsule_intersect_ray(const GeoCapsule* capsule, const GeoRay* ray, GeoVector* outNormal) {
  const GeoVector linePos       = geo_line_closest_point_ray(&capsule->line, ray);
  const GeoSphere closestSphere = {.point = linePos, .radius = capsule->radius};
  const f32       rayHitT       = geo_sphere_intersect_ray(&closestSphere, ray);
  if (rayHitT >= 0.0f) {
    const GeoVector hitPos = geo_ray_position(ray, rayHitT);
    *outNormal             = geo_vector_norm(geo_vector_sub(hitPos, closestSphere.point));
  }
  return rayHitT;
}

bool geo_capsule_overlap_sphere(const GeoCapsule* capsule, const GeoSphere* sphere) {
  const f32 distSqr = geo_line_distance_sqr_point(&capsule->line, sphere->point);
  return distSqr <= ((capsule->radius + sphere->radius) * (capsule->radius + sphere->radius));
}
