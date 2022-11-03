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

  switch (expr->op) {
  case ScriptOpUnary_Negate:
    return script_val_neg(val);
  case ScriptOpUnary_Invert:
    return script_val_inv(val);
  case ScriptOpUnary_Count:
    break;
  }
  diag_assert_fail("Invalid unary operation");
  UNREACHABLE
}

INLINE_HINT static ScriptVal eval_op_bin(ScriptEvalContext* ctx, const ScriptExprOpBinary* expr) {
  const ScriptVal a = eval(ctx, expr->lhs);
  const ScriptVal b = eval(ctx, expr->rhs);

  switch (expr->op) {
  case ScriptOpBinary_Equal:
    return script_bool(script_val_equal(a, b));
  case ScriptOpBinary_NotEqual:
    return script_bool(!script_val_equal(a, b));
  case ScriptOpBinary_Less:
    return script_bool(script_val_less(a, b));
  case ScriptOpBinary_LessOrEqual:
    return script_bool(!script_val_greater(a, b));
  case ScriptOpBinary_Greater:
    return script_bool(script_val_greater(a, b));
  case ScriptOpBinary_GreaterOrEqual:
    return script_bool(!script_val_less(a, b));
  case ScriptOpBinary_Add:
    return script_val_add(a, b);
  case ScriptOpBinary_Sub:
    return script_val_sub(a, b);
  case ScriptOpBinary_Mul:
    return script_val_mul(a, b);
  case ScriptOpBinary_Div:
    return script_val_div(a, b);
  case ScriptOpBinary_RetRight:
    return b; // NOTE: Even though we return rhs we still evaluate both lhs and rhs expressions.
  case ScriptOpBinary_Count:
    break;
  }
  diag_assert_fail("Invalid binary operation");
  UNREACHABLE
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

ScriptVal script_eval_readonly(const ScriptDoc* doc, const ScriptMem* m, const ScriptExpr expr) {
  diag_assert(script_expr_readonly(doc, expr));
  ScriptEvalContext ctx = {
      .doc = doc,
      .m   = (ScriptMem*)m, // NOTE: Safe as long as the readonly invariant is maintained.
  };
  return eval(&ctx, expr);
}
