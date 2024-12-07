#include "geo_capsule.h"
#include "geo_quat.h"
#include "geo_sphere.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

GeoCapsule geo_capsule_dilate(const GeoCapsule* capsule, const f32 radius) {
  return (GeoCapsule){.line = capsule->line, .radius = capsule->radius + radius};
}

GeoCapsule geo_capsule_transform3(
    const GeoCapsule* capsule, const GeoVector offset, const GeoQuat rotation, const f32 scale) {
#ifdef VOLO_SIMD
  const SimdVec offsetVec = simd_vec_load(offset.comps);
  const SimdVec rotVec    = simd_vec_load(rotation.comps);
  const SimdVec scaleVec  = simd_vec_broadcast(scale);

  SimdVec aVec = simd_vec_load(capsule->line.a.comps);
  aVec         = simd_vec_mul(aVec, scaleVec);
  aVec         = simd_quat_rotate(rotVec, aVec);
  aVec         = simd_vec_add(aVec, offsetVec);

  SimdVec bVec = simd_vec_load(capsule->line.b.comps);
  bVec         = simd_vec_mul(bVec, scaleVec);
  bVec         = simd_quat_rotate(rotVec, bVec);
  bVec         = simd_vec_add(bVec, offsetVec);

  SimdVec radiusVec = simd_vec_broadcast(capsule->radius);
  radiusVec         = simd_vec_mul(radiusVec, scaleVec);

  GeoCapsule res;
  simd_vec_store(aVec, res.line.a.comps);
  simd_vec_store(bVec, res.line.b.comps);
  res.radius = simd_vec_x(radiusVec);
  return res;
#else
  return (GeoCapsule){
      .line   = geo_line_transform3(&capsule->line, offset, rotation, scale),
      .radius = capsule->radius * scale,
  };
#endif
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
