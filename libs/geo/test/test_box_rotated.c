#include "check_spec.h"
#include "core_array.h"
#include "geo_box_rotated.h"
#include "geo_sphere.h"

#include "utils_internal.h"

spec(box_rotated) {

  it("can test overlaps with spheres") {
    check_msg(
        geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){{.min = {0, 0, 0}, .max = {0, 0, 0}}, .rotation = geo_quat_ident},
            &(GeoSphere){.point = {0, 0, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){{.min = {0, 0, 0}, .max = {0, 0, 0}}, .rotation = geo_quat_ident},
            &(GeoSphere){.point = {0, 1, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        !geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){{.min = {0, 0, 0}, .max = {0, 0, 0}}, .rotation = geo_quat_ident},
            &(GeoSphere){.point = {0, 1.1f, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){{.min = {0, 0, 0}, .max = {1, 1, 1}}, .rotation = geo_quat_ident},
            &(GeoSphere){.point = {0, 0, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){{.min = {0, 0, 0}, .max = {1, 1, 1}}, .rotation = geo_quat_ident},
            &(GeoSphere){.point = {0, 2, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        !geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){{.min = {0, 0, 0}, .max = {1, 1, 1}}, .rotation = geo_quat_ident},
            &(GeoSphere){.point = {0, 2.1f, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){
                {.min = {0, 0, 0}, .max = {1, 1, 1}}, .rotation = geo_quat_up_to_forward},
            &(GeoSphere){.point = {0, 0, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){
                {.min = {0, 0, 0}, .max = {1, 1, 1}}, .rotation = geo_quat_up_to_forward},
            &(GeoSphere){.point = {0, 2, 0}, .radius = 1.01f}),
        "Sphere overlap check failed");
    check_msg(
        !geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){
                {.min = {0, 0, 0}, .max = {1, 1, 1}}, .rotation = geo_quat_up_to_forward},
            &(GeoSphere){.point = {0, 2.1f, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
  }

  it("can lookup the closest point in the rotated box") {
    // clang-format off
    static const struct {
      GeoVector boxMin, boxMax;
      GeoQuat boxRotation;
      GeoVector point;
      GeoVector expected;
    } g_data[] = {
        { .boxMin      = {-1, -1, -1},  .boxMax   = {1, 1, 1},
          .boxRotation = {0, 0, 0, 1}, // geo_quat_ident
          .point       = {0, 0, 0},     .expected = {0, 0, 0},
        },
        { .boxMin      = {-1, -1, -1},  .boxMax   = {1, 1, 1},
          .boxRotation = {0, 0, 0, 1}, // geo_quat_ident
          .point       = {-2, 0, 0},    .expected = {-1, 0, 0},
        },
        { .boxMin      = {-1, -1, -1},  .boxMax   = {1, 1, 1},
          .boxRotation = {0, 0, 0, 1}, // geo_quat_ident
          .point       = {0, -2, 0},    .expected = {0, -1, 0},
        },
        { .boxMin      = {-1, -1, -1},  .boxMax   = {1, 1, 1},
          .boxRotation = {0, 0, 0, 1}, // geo_quat_ident
          .point       = {-2, -2, -2},  .expected = {-1, -1, -1},
        },
        { .boxMin      = {-3, -3, -3},  .boxMax   = {-2, -2, -2},
          .boxRotation = {0, 0, 0, 1}, // geo_quat_ident
          .point       = {2, 2, 2},     .expected = {-2, -2, -2},
        },
        { .boxMin      = {-3, -1, -1},  .boxMax   = {2, 1, 1},
          .boxRotation = {0, 0, 0, 1}, // geo_quat_ident
          .point       = {3, -3, -3},   .expected = {2, -1, -1},
        },
        { .boxMin      = {-3, -1, -1},  .boxMax   = {2, 1, 1},
          .boxRotation = {0, 1, 0, 0}, // geo_quat_forward_to_backward
          .point       = {3, -3, -3},   .expected = {2, -1, -1},
        },
        { .boxMin      = {-3, -1, -1},  .boxMax   = {3, 1, 1},
          .boxRotation = {0, 0.7071068f, 0, 0.7071068f}, // geo_quat_forward_to_right
          .point       = {3, -3, -3},   .expected = {1, -1, -3},
        },
        { .boxMin      = {-3, -1, -1},  .boxMax   = {2, 1, 1},
          .boxRotation = {0, 0.7071068f, 0, 0.7071068f}, // geo_quat_forward_to_right
          .point       = {3, -3, -3},   .expected = {0.5f, -1, -2.5f},
        },
    };
    // clang-format on

    for (u32 i = 0; i != array_elems(g_data); ++i) {
      const GeoBoxRotated boxRotated = {
          .box      = {.min = g_data[i].boxMin, .max = g_data[i].boxMax},
          .rotation = g_data[i].boxRotation,
      };
      const GeoVector closest = geo_box_rotated_closest_point(&boxRotated, g_data[i].point);
      check_eq_vector(closest, g_data[i].expected);
    }
  }
}
