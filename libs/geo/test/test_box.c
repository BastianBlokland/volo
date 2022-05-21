#include "check_spec.h"
#include "core_math.h"
#include "geo_box.h"

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
    const GeoBox    b    = geo_box_inverted3();
    const GeoVector size = geo_box_size(&b);
    check(size.x < -9999999);
    check(size.y < -9999999);
    check(size.z < -9999999);
  }

  it("can check if a box is inverted") {
    const GeoBox inverted2 = geo_box_inverted2();
    const GeoBox zero2     = {0};
    const GeoBox unit2     = {{-1, -1}, {1, 1}};
    check(geo_box_is_inverted2(&inverted2));
    check(!geo_box_is_inverted2(&zero2));
    check(!geo_box_is_inverted2(&unit2));

    const GeoBox inverted3 = geo_box_inverted3();
    const GeoBox zero3     = {0};
    const GeoBox unit3     = {{-1, -1, -1}, {1, 1, 1}};
    check(geo_box_is_inverted3(&inverted3));
    check(!geo_box_is_inverted2(&zero3));
    check(!geo_box_is_inverted2(&unit3));
  }

  it("creates a zero-sized box around a point when encapsulating it in inverted box") {
    const GeoVector p = {.1337f, -42, 123};
    GeoBox          b = geo_box_inverted3();
    b                 = geo_box_encapsulate3(&b, p);
    check_eq_vector(geo_box_center(&b), p);
    check_eq_vector(geo_box_size(&b), geo_vector(0));
  }

  it("expands to fit the given points when encapsulating points") {
    const GeoVector p1 = {.1337f, 0, -1};
    const GeoVector p2 = {.1337f, 0, +2};
    const GeoVector p3 = {.1337f, 0, +1};
    GeoBox          b  = geo_box_inverted3();
    b                  = geo_box_encapsulate3(&b, p1);
    b                  = geo_box_encapsulate3(&b, p2);
    b                  = geo_box_encapsulate3(&b, p3);

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

  it("can transform a box") {
    const GeoBox    orgBox  = {{-1, -1, -1}, {1, 1, 1}};
    const GeoVector orgSize = geo_box_size(&orgBox);

    const GeoVector offset   = geo_vector(2, 3, -1);
    const GeoQuat   rotation = geo_quat_angle_axis(geo_up, 90 * math_deg_to_rad);
    const f32       scale    = 2.0f;
    const GeoBox    transBox = geo_box_transform3(&orgBox, offset, rotation, scale);

    check_eq_vector(geo_box_size(&transBox), geo_vector_mul(orgSize, scale));
    check_eq_vector(transBox.min, geo_vector(0, 1, -3));
    check_eq_vector(transBox.max, geo_vector(4, 5, 1));
  }

  it("can compute the bounding box of a sphere") {
    {
      const GeoVector p      = {5, 0, 0};
      const f32       radius = 1.0f;
      const GeoBox    box    = geo_box_from_sphere(p, radius);

      check_eq_vector(geo_box_size(&box), geo_vector(2, 2, 2));
      check_eq_vector(box.min, geo_vector(4, -1, -1));
      check_eq_vector(box.max, geo_vector(6, 1, 1));
    }
    {
      const GeoVector p      = {5, -1, 0};
      const f32       radius = 1.5f;
      const GeoBox    box    = geo_box_from_sphere(p, radius);

      check_eq_vector(geo_box_size(&box), geo_vector(3, 3, 3));
      check_eq_vector(box.min, geo_vector(3.5f, -2.5f, -1.5f));
      check_eq_vector(box.max, geo_vector(6.5f, 0.5f, 1.5f));
    }
  }

  it("can compute the bounding box of a cone") {
    {
      const GeoVector coneBottom = {5, 0, 0};
      const GeoVector coneTop    = {5, 1, 0};
      const f32       coneRadius = 1.0f;
      const GeoBox    box        = geo_box_from_cone(coneBottom, coneTop, coneRadius);

      check_eq_vector(geo_box_size(&box), geo_vector(2, 1, 2));
      check_eq_vector(box.min, geo_vector(4, 0, -1));
      check_eq_vector(box.max, geo_vector(6, 1, 1));
    }
    {
      const GeoVector coneBottom = {5, 0, 0};
      const GeoVector coneTop    = {5, 0, 1};
      const f32       coneRadius = 2.0f;
      const GeoBox    box        = geo_box_from_cone(coneBottom, coneTop, coneRadius);

      check_eq_vector(geo_box_size(&box), geo_vector(4, 4, 1));
      check_eq_vector(box.min, geo_vector(3, -2, 0));
      check_eq_vector(box.max, geo_vector(7, 2, 1));
    }
  }
}
