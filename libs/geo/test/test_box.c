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

  it("can be created from a center and a size") {
    const GeoVector center = {1, 2, 3};
    const GeoVector size   = {2, 4, 6};
    const GeoBox    b      = geo_box_from_center(center, size);
    check_eq_vector(geo_box_center(&b), center);
    check_eq_vector(geo_box_size(&b), size);
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
    b                 = geo_box_encapsulate(&b, p);
    check_eq_vector(geo_box_center(&b), p);
    check_eq_vector(geo_box_size(&b), geo_vector(0));
  }

  it("expands to fit the given points when encapsulating points") {
    const GeoVector p1 = {.1337f, 0, -1};
    const GeoVector p2 = {.1337f, 0, +2};
    const GeoVector p3 = {.1337f, 0, +1};
    GeoBox          b  = geo_box_inverted3();
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

  it("can compute the bounding box of a cylinder") {
    {
      const GeoVector cylinderBottom = {5, 0, 0};
      const GeoVector cylinderTop    = {5, 1, 0};
      const f32       cylinderRadius = 1.0f;
      const GeoBox    box = geo_box_from_cylinder(cylinderBottom, cylinderTop, cylinderRadius);

      check_eq_vector(geo_box_size(&box), geo_vector(2, 1, 2));
      check_eq_vector(box.min, geo_vector(4, 0, -1));
      check_eq_vector(box.max, geo_vector(6, 1, 1));
    }
    {
      const GeoVector cylinderBottom = {5, 0, 0};
      const GeoVector cylinderTop    = {5, 0, 1};
      const f32       cylinderRadius = 2.0f;
      const GeoBox    box = geo_box_from_cylinder(cylinderBottom, cylinderTop, cylinderRadius);

      check_eq_vector(geo_box_size(&box), geo_vector(4, 4, 1));
      check_eq_vector(box.min, geo_vector(3, -2, 0));
      check_eq_vector(box.max, geo_vector(7, 2, 1));
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

  it("can compute the bounding box of a line") {
    const GeoVector lineStart = {6, 0, 0};
    const GeoVector lineEnd   = {5, 1, 0};
    const GeoBox    box       = geo_box_from_line(lineStart, lineEnd);

    check_eq_vector(geo_box_size(&box), geo_vector(1, 1, 0));
    check_eq_vector(box.min, geo_vector(5, 0, 0));
    check_eq_vector(box.max, geo_vector(6, 1, 0));
  }

  it("can test for approximate intersection with 4 frustum planes") {
    const GeoPlane frustum[4] = {
        {.normal = geo_right, .distance = -1.0f},
        {.normal = geo_left, .distance = -2.0f},
        {.normal = geo_down, .distance = -2.0f},
        {.normal = geo_up, .distance = -1.0f},
    };
    const GeoBox inside1 = geo_box_from_sphere(geo_vector(0, 0, 0), 0.5f);
    const GeoBox inside2 = geo_box_from_sphere(geo_vector(0, 2, 0), 0.5f);
    check(geo_box_intersect_frustum4_approx(&inside1, frustum));
    check(geo_box_intersect_frustum4_approx(&inside2, frustum));

    const GeoBox onLeftEdge   = geo_box_from_sphere(geo_vector(-1, 0, 0), 0.5f);
    const GeoBox onRightEdge  = geo_box_from_sphere(geo_vector(2, 0, 0), 0.5f);
    const GeoBox onBottomEdge = geo_box_from_sphere(geo_vector(0, -1, 0), 0.5f);
    const GeoBox onTopEdge    = geo_box_from_sphere(geo_vector(0, 2, 0), 0.5f);
    check(geo_box_intersect_frustum4_approx(&onLeftEdge, frustum));
    check(geo_box_intersect_frustum4_approx(&onRightEdge, frustum));
    check(geo_box_intersect_frustum4_approx(&onBottomEdge, frustum));
    check(geo_box_intersect_frustum4_approx(&onTopEdge, frustum));

    const GeoBox outsideLeft   = geo_box_from_sphere(geo_vector(-2, 0, 0), 0.5f);
    const GeoBox outsideRight  = geo_box_from_sphere(geo_vector(3, 0, 0), 0.5f);
    const GeoBox outsideBottom = geo_box_from_sphere(geo_vector(0, -2, 0), 0.5f);
    const GeoBox outsideTop    = geo_box_from_sphere(geo_vector(0, 3, 0), 0.5f);

    check(!geo_box_intersect_frustum4_approx(&outsideLeft, frustum));
    check(!geo_box_intersect_frustum4_approx(&outsideRight, frustum));
    check(!geo_box_intersect_frustum4_approx(&outsideBottom, frustum));
    check(!geo_box_intersect_frustum4_approx(&outsideTop, frustum));

    const GeoBox behind = geo_box_from_sphere(geo_vector(0, 0, -2), 0.5f);
    // NOTE: Because we only using 4 planes there is no such thing as 'behind' the frustum.
    check(geo_box_intersect_frustum4_approx(&behind, frustum));

    const GeoBox inFront = geo_box_from_sphere(geo_vector(0, 0, -2), 0.5f);
    // NOTE: Because we only using 4 planes there is no such thing as 'inFront' the frustum.
    check(geo_box_intersect_frustum4_approx(&inFront, frustum));

    const GeoBox inverted = geo_box_inverted3();
    // NOTE: Inverted boxes are considered to always be intersecting.
    check(geo_box_intersect_frustum4_approx(&inverted, frustum));
  }
}
