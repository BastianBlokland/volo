#include "check_spec.h"
#include "core_math.h"

#include "utils_internal.h"

spec(box) {

  it("is formed by a minimum and maximum point") {
    const GeoVector min = {-1, -1, -1};
    const GeoVector max = {+1, +1, +1};
    const GeoBox    b   = {min, max};
    check_eq_vector(geo_box_center(&b), geo_vector(0));
    check_eq_vector(geo_box_size(&b), geo_vector(2, 2, 2));
  }

  it("can construct an infinitely small box") {
    const GeoBox    b    = geo_box_inverted();
    const GeoVector size = geo_box_size(&b);
    check(size.x < -9999999);
    check(size.y < -9999999);
    check(size.z < -9999999);
  }

  it("creates a zero-sized box around a point when encapsulating it in inverted box") {
    const GeoVector p = {.1337f, -42, 123};
    GeoBox          b = geo_box_inverted();
    b                 = geo_box_encapsulate(&b, p);
    check_eq_vector(geo_box_center(&b), p);
    check_eq_vector(geo_box_size(&b), geo_vector(0));
  }

  it("expands to fit the given points when encapsulating points") {
    const GeoVector p1 = {.1337f, 0, -1};
    const GeoVector p2 = {.1337f, 0, +2};
    const GeoVector p3 = {.1337f, 0, +1};
    GeoBox          b  = geo_box_inverted();
    b                  = geo_box_encapsulate(&b, p1);
    b                  = geo_box_encapsulate(&b, p2);
    b                  = geo_box_encapsulate(&b, p3);

    check_eq_vector(geo_box_size(&b), geo_vector(0, 0, 3));
  }

  it("can retrieve the corners of a 3d box") {
    const GeoBox box = {{-1, -1, -1}, {1, 1, 1}};
    GeoVector    corners[8];
    geo_box_corners3(&box, corners);

    check_eq_vector(corners[0], geo_vector(-1, -1, -1));
    check_eq_vector(corners[1], geo_vector(-1, -1, 1));
    check_eq_vector(corners[2], geo_vector(1, -1, -1));
    check_eq_vector(corners[3], geo_vector(1, -1, 1));
    check_eq_vector(corners[4], geo_vector(-1, 1, -1));
    check_eq_vector(corners[5], geo_vector(-1, 1, 1));
    check_eq_vector(corners[6], geo_vector(1, 1, -1));
    check_eq_vector(corners[7], geo_vector(1, 1, 1));
  }
}
