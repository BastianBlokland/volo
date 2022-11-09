#include "check_spec.h"
#include "geo_capsule.h"
#include "geo_sphere.h"

spec(capsule) {

  it("can test overlaps with spheres") {
    check_msg(
        geo_capsule_overlap_sphere(
            &(GeoCapsule){.line = {{0, 0, 0}, {0, 0, 0}}, .radius = 1.0f},
            &(GeoSphere){.point = {0, 0, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        geo_capsule_overlap_sphere(
            &(GeoCapsule){.line = {{0, 0, 0}, {0, 0, 0}}, .radius = 1.0f},
            &(GeoSphere){.point = {0, 2.0f, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        !geo_capsule_overlap_sphere(
            &(GeoCapsule){.line = {{0, 0, 0}, {0, 0, 0}}, .radius = 1.0f},
            &(GeoSphere){.point = {0, 2.1f, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        geo_capsule_overlap_sphere(
            &(GeoCapsule){.line = {{0, 0, 0}, {0, 0, 2}}, .radius = 1.0f},
            &(GeoSphere){.point = {0, 2, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        !geo_capsule_overlap_sphere(
            &(GeoCapsule){.line = {{0, 0, 0}, {0, 0, 2}}, .radius = 1.0f},
            &(GeoSphere){.point = {0, 2.1f, 0}, .radius = 1.0f}),
        "Sphere overlap check failed");
    check_msg(
        geo_capsule_overlap_sphere(
            &(GeoCapsule){.line = {{0, 0, 0}, {0, 0, 2}}, .radius = 0.2f},
            &(GeoSphere){.point = {0, 0, 2.8f}, .radius = 0.6f}),
        "Sphere overlap check failed");
    check_msg(
        !geo_capsule_overlap_sphere(
            &(GeoCapsule){.line = {{0, 0, 0}, {0, 0, 2}}, .radius = 0.2f},
            &(GeoSphere){.point = {0, 0, 2.81f}, .radius = 0.6f}),
        "Sphere overlap check failed");
  }
}
