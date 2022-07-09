#include "check_spec.h"
#include "geo_line.h"

#include "utils_internal.h"

spec(line) {

  it("can compute its length") {
    const GeoLine line = {{1, 2, 3}, {1, 2, 5}};

    check_eq_float(geo_line_length(&line), 2, 1e-6);
    check_eq_float(geo_line_length_sqr(&line), 4, 1e-6);
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
  }
}
