#include "utils_internal.h"

#define test_geo_quat_threshold 1e-6f
#define test_geo_vector_threshold 1e-5f

void check_eq_quat_impl(CheckTestContext* _testCtx, const GeoQuat a, const GeoQuat b) {
  check_eq_float(a.x, b.x, test_geo_quat_threshold);
  check_eq_float(a.y, b.y, test_geo_quat_threshold);
  check_eq_float(a.z, b.z, test_geo_quat_threshold);
  check_eq_float(a.w, b.w, test_geo_quat_threshold);
}

void check_eq_vector_impl(CheckTestContext* _testCtx, const GeoVector a, const GeoVector b) {
  check_eq_float(a.x, b.x, test_geo_vector_threshold);
  check_eq_float(a.y, b.y, test_geo_vector_threshold);
  check_eq_float(a.z, b.z, test_geo_vector_threshold);
  check_eq_float(a.w, b.w, test_geo_vector_threshold);
}
