#include "script_eval.h"
#include "script_optimize.h"

static ScriptExpr opt_static_eval_rewriter(void* ctx, ScriptDoc* doc, const ScriptExpr e) {
  (void)ctx;
  if (script_expr_static(doc, e)) {
    const ScriptEvalResult evalRes = script_eval(doc, e, null, null, null);
    if (!script_panic_valid(&evalRes.panic)) {
      return script_add_value(doc, script_expr_range(doc, e), evalRes.val);
    }
  }
  return e;
}

ScriptExpr script_optimize(ScriptDoc* doc, ScriptExpr e) {

  // Pre-evaluate static expressions.
  e = script_expr_rewrite(doc, e, null, opt_static_eval_rewriter);

  return e;
}
