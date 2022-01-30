#include "core_diag.h"
#include "core_math.h"
#include "geo_plane.h"

static void assert_normalized(const GeoVector v) {
  MAYBE_UNUSED const f32 sqrMag = geo_vector_mag_sqr(v);
  diag_assert_msg(math_abs(sqrMag - 1) < 1e-4, "Given vector is not normalized");
}

GeoPlane geo_plane_at(const GeoVector normal, const GeoVector position) {
  assert_normalized(normal);
  return (GeoPlane){.normal = normal, .distance = -geo_vector_dot(normal, position)};
}
