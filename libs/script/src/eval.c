#include "core_diag.h"
#include "script_eval.h"
#include "script_mem.h"
#include "script_val.h"

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
  const ScriptVal val = eval(ctx, expr->arg1);

  switch (expr->op) {
  case ScriptOpUnary_Negate:
    return script_val_neg(val);
  case ScriptOpUnary_Invert:
    return script_val_inv(val);
  case ScriptOpUnary_Normalize:
    return script_val_norm(val);
  case ScriptOpUnary_Magnitude:
    return script_val_mag(val);
  case ScriptOpUnary_GetX:
    return script_val_get_x(val);
  case ScriptOpUnary_GetY:
    return script_val_get_y(val);
  case ScriptOpUnary_GetZ:
    return script_val_get_z(val);
  case ScriptOpUnary_Count:
    break;
  }
  diag_assert_fail("Invalid unary operation");
  UNREACHABLE
}

INLINE_HINT static ScriptVal eval_op_bin(ScriptEvalContext* ctx, const ScriptExprOpBinary* expr) {
  const ScriptVal a = eval(ctx, expr->arg1);
  switch (expr->op) {
  case ScriptOpBinary_Equal:
    return script_bool(script_val_equal(a, eval(ctx, expr->arg2)));
  case ScriptOpBinary_NotEqual:
    return script_bool(!script_val_equal(a, eval(ctx, expr->arg2)));
  case ScriptOpBinary_Less:
    return script_bool(script_val_less(a, eval(ctx, expr->arg2)));
  case ScriptOpBinary_LessOrEqual:
    return script_bool(!script_val_greater(a, eval(ctx, expr->arg2)));
  case ScriptOpBinary_Greater:
    return script_bool(script_val_greater(a, eval(ctx, expr->arg2)));
  case ScriptOpBinary_GreaterOrEqual:
    return script_bool(!script_val_less(a, eval(ctx, expr->arg2)));
  case ScriptOpBinary_LogicAnd:
    return script_bool(script_truthy(a) && script_truthy(eval(ctx, expr->arg2)));
  case ScriptOpBinary_LogicOr:
    return script_bool(script_truthy(a) || script_truthy(eval(ctx, expr->arg2)));
  case ScriptOpBinary_NullCoalescing:
    return script_val_has(a) ? a : eval(ctx, expr->arg2);
  case ScriptOpBinary_Add:
    return script_val_add(a, eval(ctx, expr->arg2));
  case ScriptOpBinary_Sub:
    return script_val_sub(a, eval(ctx, expr->arg2));
  case ScriptOpBinary_Mul:
    return script_val_mul(a, eval(ctx, expr->arg2));
  case ScriptOpBinary_Div:
    return script_val_div(a, eval(ctx, expr->arg2));
  case ScriptOpBinary_Distance:
    return script_val_dist(a, eval(ctx, expr->arg2));
  case ScriptOpBinary_Angle:
    return script_val_angle(a, eval(ctx, expr->arg2));
  case ScriptOpBinary_RetRight:
    return eval(ctx, expr->arg2);
  case ScriptOpBinary_Count:
    break;
  }
  diag_assert_fail("Invalid binary operation");
  UNREACHABLE
}

INLINE_HINT static ScriptVal eval_op_ter(ScriptEvalContext* ctx, const ScriptExprOpTernary* expr) {
  switch (expr->op) {
  case ScriptOpTernary_ComposeVector3: {
    const ScriptVal valX = eval(ctx, expr->arg1);
    const ScriptVal valY = eval(ctx, expr->arg2);
    const ScriptVal valZ = eval(ctx, expr->arg3);
    return script_val_compose_vector3(valX, valY, valZ);
  }
  case ScriptOpTernary_Select: {
    const ScriptVal condition = eval(ctx, expr->arg1);
    return script_truthy(condition) ? eval(ctx, expr->arg2) : eval(ctx, expr->arg3);
  }
  case ScriptOpTernary_Count:
    break;
  }
  diag_assert_fail("Invalid ternary operation");
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
  case ScriptExprType_OpTernary:
    return eval_op_ter(ctx, &expr_data(ctx, expr)->data_op_ternary);
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
