#include "check_spec.h"
#include "core_math.h"

#include "utils_internal.h"

spec(matrix) {

  it("returns an identity matrix when multiplying two identity matrices") {
    const GeoMatrix ident = geo_matrix_ident();
    check_eq_matrix(geo_matrix_mul(&ident, &ident), geo_matrix_ident());
  }

  it("returns the dot products of the rows and columns when multiplying two matrices") {
    {
      const GeoMatrix mA = {
          .columns[0] = {1, 3},
          .columns[1] = {2, 4},
      };
      const GeoMatrix mB = {
          .columns[0] = {2, 1},
          .columns[1] = {0, 2},
      };
      const GeoMatrix mExpected = {
          .columns[0] = {4, 10},
          .columns[1] = {4, 8},
      };
      check_eq_matrix(geo_matrix_mul(&mA, &mB), mExpected);
    }
    {
      const GeoMatrix mA = {
          .columns[0] = {2, 1},
          .columns[1] = {0, 2},
      };
      const GeoMatrix mB = {
          .columns[0] = {1, 3},
          .columns[1] = {2, 4},
      };
      const GeoMatrix mExpected = {
          .columns[0] = {2, 7},
          .columns[1] = {4, 10},
      };
      check_eq_matrix(geo_matrix_mul(&mA, &mB), mExpected);
    }
  }

  it("returns the dot products with the rows when transforming a vector") {
    const GeoMatrix m = {
        .columns[0] = {1, 0, 0},
        .columns[1] = {-1, -3, 0},
        .columns[2] = {2, 1, 1},
    };
    check_eq_vector(geo_matrix_transform(&m, geo_vector(2, 1)), geo_vector(1, -3));
  }

  it("exchanges the rows and columns when transposing") {
    const GeoMatrix m = {
        .columns[0] = {1, 4, 7},
        .columns[1] = {2, 5, 8},
        .columns[2] = {3, 6, 9},
    };
    const GeoMatrix t = {
        .columns[0] = {1, 2, 3},
        .columns[1] = {4, 5, 6},
        .columns[2] = {7, 8, 9},
    };
    check_eq_matrix(geo_matrix_transpose(&m), t);
    check_eq_matrix(geo_matrix_transpose(&t), m);
  }

  it("returns the same matrix when multiplying with the identity matrix") {
    const GeoMatrix mA = {
        .columns[0] = {1, 4, 7},
        .columns[1] = {2, 5, 8},
        .columns[2] = {3, 6, 9},
    };
    const GeoMatrix mB = geo_matrix_ident();
    check_eq_matrix(geo_matrix_mul(&mA, &mB), mA);
  }

  it("returns the same vector when transforming with the identity matrix") {
    const GeoVector v = geo_vector(2, 3, 4);
    const GeoMatrix m = geo_matrix_ident();
    check_eq_vector(geo_matrix_transform(&m, v), v);
  }

  it("applies translation as an offset to position vectors") {
    const GeoMatrix m = geo_matrix_translate(geo_vector(-1, 2, .1f));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(0, 0, 0, 1)), geo_vector(-1, 2, .1f, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(1, 1, 1, 1)), geo_vector(0, 3, 1.1f, 1));
    check_eq_vector(
        geo_matrix_transform(&m, geo_vector(-1, -1, -1, 1)), geo_vector(-2, 1, -.9f, 1));
  }

  it("ignores translation for direction vectors") {
    const GeoMatrix m = geo_matrix_translate(geo_vector(-1, 2, .1f));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(0, 0, 0, 0)), geo_vector(0, 0, 0, 0));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(1, 1, 1, 0)), geo_vector(1, 1, 1, 0));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(-1, -1, -1, 0)), geo_vector(-1, -1, -1, 0));
  }

  it("applies scale as a multiplier to position and direction vectors") {
    const GeoMatrix m = geo_matrix_scale(geo_vector(1, 2, 3));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(0, 0, 0, 1)), geo_vector(0, 0, 0, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(1, 1, 1, 1)), geo_vector(1, 2, 3, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(2, 3, 4, 1)), geo_vector(2, 6, 12, 1));
    check_eq_vector(geo_matrix_transform(&m, geo_vector(2, 3, 4, 0)), geo_vector(2, 6, 12, 0));
  }
}
