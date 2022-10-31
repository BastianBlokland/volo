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

INLINE_HINT static ScriptVal eval_value(ScriptEvalContext* ctx, const ScriptExprValue* expr) {
  return dynarray_begin_t(&ctx->doc->values, ScriptVal)[expr->valId];
}

INLINE_HINT static ScriptVal eval_load(ScriptEvalContext* ctx, const ScriptExprLoad* expr) {
  return script_mem_get(ctx->m, expr->key);
}

INLINE_HINT static ScriptVal eval_store(ScriptEvalContext* ctx, const ScriptExprStore* expr) {
  const ScriptVal val = eval(ctx, expr->val);
  script_mem_set(ctx->m, expr->key, val);
  return val;
}

INLINE_HINT static ScriptVal eval_op_una(ScriptEvalContext* ctx, const ScriptExprOpUnary* expr) {
  const ScriptVal val = eval(ctx, expr->val);
  return script_op_unary(val, expr->op);
}

INLINE_HINT static ScriptVal eval_op_bin(ScriptEvalContext* ctx, const ScriptExprOpBinary* expr) {
  const ScriptVal lhs = eval(ctx, expr->lhs);
  const ScriptVal rhs = eval(ctx, expr->rhs);
  return script_op_binary(lhs, rhs, expr->op);
}

static ScriptVal eval(ScriptEvalContext* ctx, const ScriptExpr expr) {
  switch (script_expr_type(ctx->doc, expr)) {
  case ScriptExprType_Value:
    return eval_value(ctx, &expr_data(ctx, expr)->data_value);
  case ScriptExprType_Load:
    return eval_load(ctx, &expr_data(ctx, expr)->data_load);
  case ScriptExprType_Store:
    return eval_store(ctx, &expr_data(ctx, expr)->data_store);
  case ScriptExprType_OpUnary:
    return eval_op_una(ctx, &expr_data(ctx, expr)->data_op_unary);
  case ScriptExprType_OpBinary:
    return eval_op_bin(ctx, &expr_data(ctx, expr)->data_op_binary);
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
