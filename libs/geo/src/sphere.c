#include "core/array.h"
#include "core/diag.h"
#include "core/float.h"
#include "core/intrinsic.h"
#include "geo/quat.h"
#include "geo/sphere.h"

#ifdef VOLO_SIMD
#include "core/simd.h"
#endif

GeoSphere geo_sphere_dilate(const GeoSphere* sphere, const f32 radius) {
  return (GeoSphere){.point = sphere->point, .radius = sphere->radius + radius};
}

GeoSphere geo_sphere_transform3(
    const GeoSphere* sphere, const GeoVector offset, const GeoQuat rotation, const f32 scale) {

#ifdef VOLO_SIMD
  const SimdVec scaleVec = simd_vec_broadcast(scale);

  SimdVec radiusVec = simd_vec_broadcast(sphere->radius);
  simd_vec_mul(radiusVec, scaleVec);

  SimdVec pointVec = simd_vec_load(sphere->point.comps);
  pointVec         = simd_vec_mul(pointVec, scaleVec);
  pointVec         = simd_quat_rotate(simd_vec_load(rotation.comps), pointVec);
  pointVec         = simd_vec_add(pointVec, simd_vec_load(offset.comps));

  GeoSphere res;
  simd_vec_store(pointVec, res.point.comps);
  res.radius = simd_vec_x(radiusVec);
  return res;
#else
  GeoVector point;
  point = geo_vector_mul(sphere->point, scale);
  point = geo_quat_rotate(rotation, point);
  point = geo_vector_add(point, offset);

  return (GeoSphere){
      .point  = point,
      .radius = sphere->radius * scale,
  };
#endif
}

f32 geo_sphere_intersect_ray(const GeoSphere* sphere, const GeoRay* ray) {
  /**
   * Additional information:
   * https://gdbooks.gitbooks.io/3dcollisions/content/Chapter3/raycast_sphere.html
   */

  const GeoVector toCenter        = geo_vector_sub(sphere->point, ray->point);
  const f32       toCenterDistSqr = geo_vector_mag_sqr(toCenter);
  const f32       a               = geo_vector_dot(toCenter, ray->dir);
  const f32       b               = toCenterDistSqr - (a * a);

  // Test if there is no collision.
  if ((sphere->radius * sphere->radius - toCenterDistSqr + a * a) < 0.0f) {
    return -1.0f; // No collision.
  }

  const f32 c = UNLIKELY(b < f32_epsilon) ? 0.0f : intrinsic_sqrt_f32(b);
  const f32 d = (sphere->radius * sphere->radius) - (c * c);
  const f32 f = UNLIKELY(d < f32_epsilon) ? 0.0f : intrinsic_sqrt_f32(d);

  // Test if ray is inside.
  if (toCenterDistSqr < sphere->radius * sphere->radius) {
    return a + f; // Reverse direction.
  }

  // Normal intersection.
  return a - f;
}

f32 geo_sphere_intersect_ray_info(
    const GeoSphere* sphere, const GeoRay* ray, GeoVector* outNormal) {
  const f32 hitT = geo_sphere_intersect_ray(sphere, ray);
  if (hitT >= 0) {
    *outNormal = geo_vector_norm(geo_vector_sub(geo_ray_position(ray, hitT), sphere->point));
  }
  diag_assert_msg(hitT <= 1e5f, "Invalid intersection: {}", fmt_float(hitT));
  return hitT;
}

bool geo_sphere_overlap(const GeoSphere* a, const GeoSphere* b) {
  const f32 distSqr = geo_vector_mag_sqr(geo_vector_sub(b->point, a->point));
  return distSqr <= ((a->radius + b->radius) * (a->radius + b->radius));
}

bool geo_sphere_overlap_box(const GeoSphere* sphere, const GeoBox* box) {
  const GeoVector closest = geo_box_closest_point(box, sphere->point);
  const f32       distSqr = geo_vector_mag_sqr(geo_vector_sub(closest, sphere->point));
  return distSqr <= (sphere->radius * sphere->radius);
}

bool geo_sphere_overlap_frustum(
    const GeoSphere* sphere, const GeoVector frustum[PARAM_ARRAY_SIZE(8)]) {
  const GeoPlane frustumPlanes[] = {
      geo_plane_at_triangle(frustum[3], frustum[6], frustum[2]), // Right.
      geo_plane_at_triangle(frustum[1], frustum[4], frustum[0]), // Left.
      geo_plane_at_triangle(frustum[2], frustum[5], frustum[1]), // Up.
      geo_plane_at_triangle(frustum[4], frustum[7], frustum[0]), // Down.
      geo_plane_at_triangle(frustum[4], frustum[5], frustum[6]), // Back.
      geo_plane_at_triangle(frustum[2], frustum[1], frustum[0]), // Front.
  };
  array_for_t(frustumPlanes, GeoPlane, plane) {
    if ((geo_vector_dot(sphere->point, plane->normal) - plane->distance) < -sphere->radius) {
      return false;
    }
  }
  return true;
}
