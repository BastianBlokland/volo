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
