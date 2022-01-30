#include "check_spec.h"
#include "core_math.h"
#include "geo_plane.h"

#include "utils_internal.h"

spec(plane) {

  it("can be constructed from a normal and a point") {
    const GeoVector position = {1, -42, 2};

    const GeoPlane p1 = geo_plane_at(geo_up, position);
    check_eq_vector(p1.normal, geo_up);
    check_eq_float(p1.distance, 42, 1e-6f);

    const GeoPlane p2 = geo_plane_at(geo_down, position);
    check_eq_vector(p2.normal, geo_down);
    check_eq_float(p2.distance, -42, 1e-6f);

    const GeoVector n3 = geo_vector_norm(geo_vector(1, 2, 3));
    const GeoPlane  p3 = geo_plane_at(n3, position);
    check_eq_vector(p3.normal, n3);
    check_eq_float(p3.distance, 20.57911, 1e-4f);
  }
}
