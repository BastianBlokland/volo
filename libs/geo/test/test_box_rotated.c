#include "check_spec.h"
#include "core_math.h"
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
            &(GeoSphere){.point = {0, 2, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        !geo_box_rotated_overlap_sphere(
            &(GeoBoxRotated){
                {.min = {0, 0, 0}, .max = {1, 1, 1}}, .rotation = geo_quat_up_to_forward},
            &(GeoSphere){.point = {0, 2.1f, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
  }
}
