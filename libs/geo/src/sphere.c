#include "core_float.h"
#include "core_math.h"
#include "geo_sphere.h"

#include "intrinsic_internal.h"

f32 geo_sphere_intersect_ray(const GeoSphere* sphere, const GeoRay* ray) {
  /**
   * Additional information:
   * https://gdbooks.gitbooks.io/3dcollisions/content/Chapter3/raycast_sphere.html
   */

  const GeoVector toCenter        = geo_vector_sub(sphere->point, ray->point);
  const f32       toCenterDistSqr = geo_vector_mag_sqr(toCenter);
  const f32       a               = geo_vector_dot(toCenter, ray->dir);
  const f32       b               = toCenterDistSqr - (a * a);
  const f32       c               = b < f32_epsilon ? 0 : intrinsic_sqrt_f32(b);

  // Test if there is no collision.
  if ((sphere->radius * sphere->radius - toCenterDistSqr + a * a) < 0.0f) {
    return -1.0f; // No collision.
  }

  const f32 f = intrinsic_sqrt_f32((sphere->radius * sphere->radius) - (c * c));

  // Test if ray is inside.
  if (toCenterDistSqr < sphere->radius * sphere->radius) {
    return a + f; // Reverse direction.
  }

  // Normal intersection.
  return a - f;
}

bool geo_sphere_overlap_box(const GeoSphere* sphere, const GeoBox* box) {
  const GeoVector closest = geo_box_closest_point(box, sphere->point);
  const f32       distSqr = geo_vector_mag_sqr(geo_vector_sub(closest, sphere->point));
  return distSqr <= (sphere->radius * sphere->radius);
}
