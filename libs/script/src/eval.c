#include "core_diag.h"
#include "script_eval.h"
#include "script_mem.h"

#include "doc_internal.h"

typedef struct {
  const ScriptDoc* doc;
  ScriptMem*       m;
} ScriptEvalContext;

static ScriptVal eval(ScriptEvalContext*, ScriptExpr);

INLINE_HINT static const ScriptExprData* expr_data(ScriptEvalContext* ctx, const ScriptExpr expr) {
  return &dynarray_begin_t(&ctx->doc->exprs, ScriptExprData)[expr];
}

INLINE_HINT static ScriptVal eval_value(ScriptEvalContext* ctx, const ScriptExprValue* valExpr) {
  return dynarray_begin_t(&ctx->doc->values, ScriptVal)[valExpr->valId];
}

INLINE_HINT static ScriptVal eval_load(ScriptEvalContext* ctx, const ScriptExprLoad* loadExpr) {
  return script_mem_get(ctx->m, loadExpr->key);
}

INLINE_HINT static ScriptVal eval_op_bin(ScriptEvalContext* ctx, const ScriptExprOpBin* binOpExpr) {
  const ScriptVal lhs = eval(ctx, binOpExpr->lhs);
  const ScriptVal rhs = eval(ctx, binOpExpr->rhs);
  return script_op_bin(lhs, rhs, binOpExpr->op);
}

static ScriptVal eval(ScriptEvalContext* ctx, const ScriptExpr expr) {
  switch (script_expr_type(ctx->doc, expr)) {
  case ScriptExprType_Value:
    return eval_value(ctx, &expr_data(ctx, expr)->data_value);
  case ScriptExprType_Load:
    return eval_load(ctx, &expr_data(ctx, expr)->data_load);
  case ScriptExprType_OpBin:
    return eval_op_bin(ctx, &expr_data(ctx, expr)->data_op_bin);
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Unknown expression type");
  UNREACHABLE
}

ScriptVal script_eval(const ScriptDoc* doc, ScriptMem* m, const ScriptExpr expr) {
  ScriptEvalContext ctx = {
      .doc = doc,
      .m   = m,
  };
  return eval(&ctx, expr);
}
