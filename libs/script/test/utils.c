#include "utils_internal.h"

void check_eq_tok_impl(
    CheckTestContext* ctx, const ScriptToken* a, const ScriptToken* b, const SourceLoc src) {

  if (UNLIKELY(!script_token_equal(a, b))) {
    const String msg = fmt_write_scratch("{} == {}", script_token_fmt(a), script_token_fmt(b));
    check_report_error(ctx, msg, src);
  }
}

void check_neq_tok_impl(
    CheckTestContext* ctx, const ScriptToken* a, const ScriptToken* b, const SourceLoc src) {

  if (UNLIKELY(script_token_equal(a, b))) {
    const String msg = fmt_write_scratch("{} != {}", script_token_fmt(a), script_token_fmt(b));
    check_report_error(ctx, msg, src);
  }
}

void check_eq_val_impl(
    CheckTestContext* ctx, const ScriptVal a, const ScriptVal b, const SourceLoc src) {

  if (UNLIKELY(!script_val_equal(a, b))) {
    const String msg = fmt_write_scratch("{} == {}", script_val_fmt(a), script_val_fmt(b));
    check_report_error(ctx, msg, src);
  }
}

void check_neq_val_impl(
    CheckTestContext* ctx, const ScriptVal a, const ScriptVal b, const SourceLoc src) {

  if (UNLIKELY(script_val_equal(a, b))) {
    const String msg = fmt_write_scratch("{} != {}", script_val_fmt(a), script_val_fmt(b));
    check_report_error(ctx, msg, src);
  }
}

void check_less_val_impl(
    CheckTestContext* ctx, const ScriptVal a, const ScriptVal b, const SourceLoc src) {

  if (UNLIKELY(!script_val_less(a, b))) {
    const String msg = fmt_write_scratch("{} < {}", script_val_fmt(a), script_val_fmt(b));
    check_report_error(ctx, msg, src);
  }
}

void check_greater_val_impl(
    CheckTestContext* ctx, const ScriptVal a, const ScriptVal b, const SourceLoc src) {

  if (UNLIKELY(!script_val_greater(a, b))) {
    const String msg = fmt_write_scratch("{} > {}", script_val_fmt(a), script_val_fmt(b));
    check_report_error(ctx, msg, src);
  }
}
