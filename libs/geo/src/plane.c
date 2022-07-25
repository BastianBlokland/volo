#include "core_diag.h"
#include "core_math.h"
#include "geo_plane.h"

static void assert_normalized(const GeoVector v) {
  MAYBE_UNUSED const f32 sqrMag = geo_vector_mag_sqr(v);
  diag_assert_msg(math_abs(sqrMag - 1) < 1e-4, "Given vector is not normalized");
}

GeoPlane geo_plane_at(const GeoVector normal, const GeoVector position) {
  assert_normalized(normal);
  return (GeoPlane){.normal = normal, .distance = geo_vector_dot(normal, position)};
}

GeoVector geo_plane_pos(const GeoPlane* plane) {
  return geo_vector_mul(plane->normal, plane->distance);
}

GeoVector geo_plane_closest_point(const GeoPlane* plane, const GeoVector point) {
  const f32 dist = geo_vector_dot(plane->normal, point) + plane->distance;
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
