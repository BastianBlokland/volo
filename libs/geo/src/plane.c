#include "core_diag.h"
#include "core_math.h"
#include "geo_plane.h"

#define geo_norm_threshold 1e-5f

MAYBE_UNUSED static void geo_plane_assert_norm(const GeoVector vec) {
  diag_assert_msg(
      math_abs(geo_vector_mag_sqr(vec) - 1.0f) < geo_norm_threshold,
      "Plane normal has to be a unit-vector");
}

GeoPlane geo_plane_at(const GeoVector normal, const GeoVector position) {
  geo_plane_assert_norm(normal);
  return (GeoPlane){.normal = normal, .distance = -geo_vector_dot(normal, position)};
}
