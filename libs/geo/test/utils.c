#include "core_alloc.h"

#include "utils_internal.h"

#define test_geo_threshold 1e-5f

static bool test_matrix_equal(const GeoMatrix* a, const GeoMatrix* b) {
  for (usize i = 0; i != 16; ++i) {
    if (math_abs(a->comps[i] - b->comps[i]) > test_geo_threshold) {
      return false;
    }
  }
  return true;
}

static bool test_quat_equal(const GeoQuat a, const GeoQuat b) {
  for (usize i = 0; i != 4; ++i) {
    if (math_abs(a.comps[i] - b.comps[i]) > test_geo_threshold) {
      return false;
    }
  }
  return true;
}

static bool test_vector_equal(const GeoVector a, const GeoVector b) {
  for (usize i = 0; i != 4; ++i) {
    if (math_abs(a.comps[i] - b.comps[i]) > test_geo_threshold) {
      return false;
    }
  }
  return true;
}

static String test_matrix_fmt_scratch(const GeoMatrix* matrix) {
  DynString str = dynstring_create_over(alloc_alloc(g_alloc_scratch, usize_kibibyte, 1));
  for (usize i = 0; i != 4; ++i) {
    fmt_write(&str, "[{}]", geo_vector_fmt(geo_matrix_row(matrix, i)));
  }
  return dynstring_view(&str);
}

void check_eq_matrix_impl(
    CheckTestContext* ctx, const GeoMatrix a, const GeoMatrix b, const SourceLoc src) {

  if (UNLIKELY(!test_matrix_equal(&a, &b))) {
    const String msg = fmt_write_scratch(
        "{} == {}", fmt_text(test_matrix_fmt_scratch(&a)), fmt_text(test_matrix_fmt_scratch(&b)));
    check_report_error(ctx, msg, src);
  }
}

void check_eq_quat_impl(
    CheckTestContext* ctx, const GeoQuat a, const GeoQuat b, const SourceLoc src) {

  if (UNLIKELY(!test_quat_equal(a, b))) {
    check_report_error(ctx, fmt_write_scratch("{} == {}", geo_quat_fmt(a), geo_quat_fmt(b)), src);
  }
}

void check_eq_vector_impl(
    CheckTestContext* ctx, const GeoVector a, const GeoVector b, const SourceLoc src) {

  if (UNLIKELY(!test_vector_equal(a, b))) {
    check_report_error(
        ctx, fmt_write_scratch("{} == {}", geo_vector_fmt(a), geo_vector_fmt(b)), src);
  }
}
