#include "core_alloc.h"

#include "utils_internal.h"

void check_eq_value_impl(
    CheckTestContext* ctx, const AiValue a, const AiValue b, const SourceLoc src) {

  if (UNLIKELY(!ai_value_equal(a, b))) {
    const String msg = fmt_write_scratch(
        "{} == {}", fmt_text(ai_value_str_scratch(a)), fmt_text(ai_value_str_scratch(b)));
    check_report_error(ctx, msg, src);
  }
}

void check_neq_value_impl(
    CheckTestContext* ctx, const AiValue a, const AiValue b, const SourceLoc src) {

  if (UNLIKELY(ai_value_equal(a, b))) {
    const String msg = fmt_write_scratch(
        "{} != {}", fmt_text(ai_value_str_scratch(a)), fmt_text(ai_value_str_scratch(b)));
    check_report_error(ctx, msg, src);
  }
}

void check_less_value_impl(
    CheckTestContext* ctx, const AiValue a, const AiValue b, const SourceLoc src) {

  if (UNLIKELY(!ai_value_less(a, b))) {
    const String msg = fmt_write_scratch(
        "{} < {}", fmt_text(ai_value_str_scratch(a)), fmt_text(ai_value_str_scratch(b)));
    check_report_error(ctx, msg, src);
  }
}

void check_greater_value_impl(
    CheckTestContext* ctx, const AiValue a, const AiValue b, const SourceLoc src) {

  if (UNLIKELY(!ai_value_greater(a, b))) {
    const String msg = fmt_write_scratch(
        "{} > {}", fmt_text(ai_value_str_scratch(a)), fmt_text(ai_value_str_scratch(b)));
    check_report_error(ctx, msg, src);
  }
}
