#include "core_diag.h"
#include "core_math.h"
#include "geo_plane.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

static void assert_normalized(const GeoVector v) {
  MAYBE_UNUSED const f32 sqrMag = geo_vector_mag_sqr(v);
  diag_assert_msg(math_abs(sqrMag - 1) < 1e-4, "Given vector is not normalized");
}

GeoPlane geo_plane_at(const GeoVector normal, const GeoVector position) {
  assert_normalized(normal);
  return (GeoPlane){.normal = normal, .distance = geo_vector_dot(normal, position)};
}

GeoPlane geo_plane_at_triangle(const GeoVector a, const GeoVector b, const GeoVector c) {
#ifdef VOLO_SIMD
  const SimdVec aVec     = simd_vec_load(a.comps);
  const SimdVec bVec     = simd_vec_load(b.comps);
  const SimdVec cVec     = simd_vec_load(c.comps);
  const SimdVec toB      = simd_vec_sub(bVec, aVec);
  const SimdVec toC      = simd_vec_sub(cVec, aVec);
  const SimdVec cross    = simd_vec_cross3(toB, toC);
  const SimdVec crossMag = simd_vec_sqrt(simd_vec_dot4(cross, cross));
  const SimdVec normal   = simd_vec_div(cross, crossMag);

  GeoPlane res;
  simd_vec_store(normal, res.normal.comps);
  res.distance = simd_vec_x(simd_vec_dot3(normal, aVec));
  return res;
#else
  const GeoVector toB    = geo_vector_sub(b, a);
  const GeoVector toC    = geo_vector_sub(c, a);
  const GeoVector normal = geo_vector_norm(geo_vector_cross3(toB, toC));
  return (GeoPlane){.normal = normal, .distance = geo_vector_dot(normal, a)};
#endif
}

GeoVector geo_plane_position(const GeoPlane* plane) {
  return geo_vector_mul(plane->normal, plane->distance);
}

GeoVector geo_plane_closest_point(const GeoPlane* plane, const GeoVector point) {
  const f32 dist = geo_vector_dot(plane->normal, point) - plane->distance;
  return geo_vector_sub(point, geo_vector_mul(plane->normal, dist));
}

f32 geo_plane_intersect_ray(const GeoPlane* plane, const GeoRay* ray) {
  const f32 dirDot = geo_vector_dot(ray->dir, plane->normal);
  if (dirDot >= 0) {
    return -1.0;
  }
  const f32 pointDot = geo_vector_dot(ray->point, plane->normal);
  const f32 t        = (plane->distance - pointDot) / dirDot;
  return t >= 0 ? t : -1.0f;
}
