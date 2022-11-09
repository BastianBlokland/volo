#include "check_spec.h"
#include "geo_line.h"

#include "utils_internal.h"

spec(line) {

  it("can compute its length") {
    {
      const GeoLine line = {{1, 2, 3}, {1, 2, 5}};
      check_eq_float(geo_line_length(&line), 2, 1e-6);
      check_eq_float(geo_line_length_sqr(&line), 4, 1e-6);
    }
    {
      const GeoLine line = {{0, 0, 0}, {0, 0, 0}};
      check_eq_float(geo_line_length(&line), 0, 1e-6);
      check_eq_float(geo_line_length_sqr(&line), 0, 1e-6);
    }
  }

  it("can compute its direction") {
    {
      const GeoLine line = {{1, 2, 3}, {1, 2, 5}};
      check_eq_vector(geo_line_direction(&line), geo_forward);
    }
    {
      const GeoLine line = {{1, 2, 3}, {1, 2, -5}};
      check_eq_vector(geo_line_direction(&line), geo_backward);
    }
    {
      const GeoLine line = {{1, 2, 3}, {2, 2, 3}};
      check_eq_vector(geo_line_direction(&line), geo_right);
    }
    {
      const GeoLine line = {{0, 0, 0}, {0, 0, 0}};
      check_eq_vector(geo_line_direction(&line), geo_forward);
    }
  }

  it("can find the time closest to the given point") {
    {
      const GeoLine line = {{0, 1, 0}, {0, 1, 5}};
      check_eq_float(geo_line_closest_time(&line, geo_vector(0, 0, 0)), 0, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(0, 1, 5)), 1, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(0, 2, -1)), 0, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(0, 3, 6)), 1, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(0, 4, 2.5f)), 0.5f, 1e-6);
    }
    {
      const GeoLine line = {{-2, -2, -2}, {2, 2, 2}};
      check_eq_float(geo_line_closest_time(&line, geo_vector(-2, -2, -2)), 0, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(-3, -3, -3)), 0, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(2, 2, 2)), 1, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(3, 3, 3)), 1, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(0, 0, 0)), 0.5f, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(1, 2, 3)), 1, 1e-6);
    }
    {
      const GeoLine line = {{0, 0, 0}, {0, 0, 0}};
      check_eq_float(geo_line_closest_time(&line, geo_vector(-2, -2, -2)), 0, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(-3, -3, -3)), 0, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(2, 2, 2)), 0, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(3, 3, 3)), 0, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(0, 0, 0)), 0, 1e-6);
      check_eq_float(geo_line_closest_time(&line, geo_vector(1, 2, 3)), 0, 1e-6);
    }
  }

  it("can find the time closest to the given ray") {
    {
      const GeoLine line = {{0, 1, 0}, {0, 1, 5}};
      const GeoRay  ray  = {.point = {1, 0, -10}, .dir = geo_up};
      check_eq_float(geo_line_closest_time_ray(&line, &ray), 0, 1e-6);
    }
    {
      const GeoLine line = {{0, 1, 0}, {0, 1, 5}};
      const GeoRay  ray  = {.point = {1, 0, 10}, .dir = geo_up};
      check_eq_float(geo_line_closest_time_ray(&line, &ray), 1, 1e-6);
    }
    {
      const GeoLine line = {{0, 0, 0}, {0, 0, 0}};
      const GeoRay  ray  = {.point = {1, 0, 10}, .dir = geo_up};
      check_eq_float(geo_line_closest_time_ray(&line, &ray), 1, 1e-6);
    }
  }

  it("can compute the distance squared to a given point") {
    {
      const GeoLine line = {{0, 1, 0}, {0, 1, 5}};
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(0, 0, 0)), 1, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(0, 1, 0)), 0, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(0, 2, 0)), 1, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(0, 1, 5)), 0, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(0, 1, 10)), 25, 1e-6);
    }
    {
      const GeoLine line = {{-2, -2, -2}, {2, 2, 2}};
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(-2, -2, -2)), 0, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(0, 0, 0)), 0, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(-3, -3, -3)), 3, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(-5, -5, -5)), 27, 1e-6);
    }
    {
      const GeoLine line = {{0, 0, 0}, {0, 0, 0}};
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(-2, -2, -2)), 0, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(0, 0, 0)), 0, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(-3, -3, -3)), 3, 1e-6);
      check_eq_float(geo_line_distance_sqr_point(&line, geo_vector(-5, -5, -5)), 27, 1e-6);
    }
  }
}
