#include "geo_capsule.h"
#include "geo_sphere.h"

GeoCapsule geo_capsule_dilate(const GeoCapsule* capsule, const f32 radius) {
  return (GeoCapsule){.line = capsule->line, .radius = capsule->radius + radius};
}

f32 geo_capsule_intersect_ray(const GeoCapsule* capsule, const GeoRay* ray) {
  const GeoVector linePos       = geo_line_closest_point_ray(&capsule->line, ray);
  const GeoSphere closestSphere = {.point = linePos, .radius = capsule->radius};
  return geo_sphere_intersect_ray(&closestSphere, ray);
}

f32 geo_capsule_intersect_ray_info(
    const GeoCapsule* capsule, const GeoRay* ray, GeoVector* outNormal) {
  const GeoVector linePos       = geo_line_closest_point_ray(&capsule->line, ray);
  const GeoSphere closestSphere = {.point = linePos, .radius = capsule->radius};
  return geo_sphere_intersect_ray_info(&closestSphere, ray, outNormal);
}

bool geo_capsule_overlap_sphere(const GeoCapsule* capsule, const GeoSphere* sphere) {
  const f32 distSqr = geo_line_distance_sqr_point(&capsule->line, sphere->point);
  return distSqr <= ((capsule->radius + sphere->radius) * (capsule->radius + sphere->radius));
}
