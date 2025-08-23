#include "check/spec.h"
#include "geo/plane.h"

#include "utils_internal.h"

spec(plane) {

  it("can be constructed from a normal and a point") {
    const GeoVector position = {1, -42, 2};

    const GeoPlane p1 = geo_plane_at(geo_up, position);
    check_eq_vector(p1.normal, geo_up);
    check_eq_float(p1.distance, -42, 1e-6f);

    const GeoPlane p2 = geo_plane_at(geo_down, position);
    check_eq_vector(p2.normal, geo_down);
    check_eq_float(p2.distance, 42, 1e-6f);

    const GeoVector n3 = geo_vector_norm(geo_vector(1, 2, 3));
    const GeoPlane  p3 = geo_plane_at(n3, position);
    check_eq_vector(p3.normal, n3);
    check_eq_float(p3.distance, -20.57911, 1e-4f);
  }

  it("can lookup a position on the surface") {
    const GeoVector position = {1, 2, 3};
    const GeoVector normal   = geo_vector_norm(geo_vector(1, 2, 3));
    const GeoPlane  plane    = geo_plane_at(normal, position);
    check_eq_vector(geo_plane_position(&plane), geo_vector(1, 2, 3));
  }

  it("can find the closest point") {
    const GeoVector position = {1, 42, 2};
    const GeoPlane  p1       = geo_plane_at(geo_up, position);

    check_eq_vector(geo_plane_closest_point(&p1, geo_vector(1, 0, 2)), geo_vector(1, 42, 2));
    check_eq_vector(geo_plane_closest_point(&p1, geo_vector(42, -42, 42)), geo_vector(42, 42, 42));
  }
}
