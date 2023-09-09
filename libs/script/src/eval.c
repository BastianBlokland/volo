#include "core_annotation.h"
#include "core_diag.h"
#include "script_eval.h"
#include "script_mem.h"
#include "script_val.h"

#include "doc_internal.h"

typedef struct {
  const ScriptDoc* doc;
  ScriptMem*       m;
  ScriptVal        vars[script_var_count];
} ScriptEvalContext;

static ScriptVal eval(ScriptEvalContext*, ScriptExpr);

INLINE_HINT static const ScriptExprData* expr_data(ScriptEvalContext* ctx, const ScriptExpr expr) {
  return dynarray_begin_t(&ctx->doc->exprData, ScriptExprData) + expr;
}

INLINE_HINT static const ScriptExpr*
expr_set_data(ScriptEvalContext* ctx, const ScriptExprSet set) {
  return dynarray_begin_t(&ctx->doc->exprSets, ScriptExpr) + set;
}

INLINE_HINT static ScriptVal eval_value(ScriptEvalContext* ctx, const ScriptExprValue* expr) {
  return dynarray_begin_t(&ctx->doc->values, ScriptVal)[expr->valId];
}

INLINE_HINT static ScriptVal eval_var_load(ScriptEvalContext* ctx, const ScriptExprVarLoad* expr) {
  return ctx->vars[expr->var];
}

INLINE_HINT static ScriptVal eval_var_store(ScriptEvalContext* ctx, const ScriptExprVarStore* exp) {
  const ScriptVal val = eval(ctx, exp->val);
  ctx->vars[exp->var] = val;
  return val;
}

INLINE_HINT static ScriptVal eval_mem_load(ScriptEvalContext* ctx, const ScriptExprMemLoad* expr) {
  return script_mem_get(ctx->m, expr->key);
}

INLINE_HINT static ScriptVal eval_mem_store(ScriptEvalContext* ctx, const ScriptExprMemStore* exp) {
  const ScriptVal val = eval(ctx, exp->val);
  script_mem_set(ctx->m, exp->key, val);
  return val;
}

INLINE_HINT static ScriptVal eval_intr(ScriptEvalContext* ctx, const ScriptExprIntrinsic* expr) {
  const ScriptExpr* args = expr_set_data(ctx, expr->argSet);
  switch (expr->intrinsic) {
  case ScriptIntrinsic_Random:
    return script_val_random();
  case ScriptIntrinsic_Negate:
    return script_val_neg(eval(ctx, args[0]));
  case ScriptIntrinsic_Invert:
    return script_val_inv(eval(ctx, args[0]));
  case ScriptIntrinsic_Normalize:
    return script_val_norm(eval(ctx, args[0]));
  case ScriptIntrinsic_Magnitude:
    return script_val_mag(eval(ctx, args[0]));
  case ScriptIntrinsic_VectorX:
    return script_val_vector_x(eval(ctx, args[0]));
  case ScriptIntrinsic_VectorY:
    return script_val_vector_y(eval(ctx, args[0]));
  case ScriptIntrinsic_VectorZ:
    return script_val_vector_z(eval(ctx, args[0]));
  case ScriptIntrinsic_RoundDown:
    return script_val_round_down(eval(ctx, args[0]));
  case ScriptIntrinsic_RoundNearest:
    return script_val_round_nearest(eval(ctx, args[0]));
  case ScriptIntrinsic_RoundUp:
    return script_val_round_up(eval(ctx, args[0]));
  case ScriptIntrinsic_Equal:
    return script_bool(script_val_equal(eval(ctx, args[0]), eval(ctx, args[1])));
  case ScriptIntrinsic_NotEqual:
    return script_bool(!script_val_equal(eval(ctx, args[0]), eval(ctx, args[1])));
  case ScriptIntrinsic_Less:
    return script_bool(script_val_less(eval(ctx, args[0]), eval(ctx, args[1])));
  case ScriptIntrinsic_LessOrEqual:
    return script_bool(!script_val_greater(eval(ctx, args[0]), eval(ctx, args[1])));
  case ScriptIntrinsic_Greater:
    return script_bool(script_val_greater(eval(ctx, args[0]), eval(ctx, args[1])));
  case ScriptIntrinsic_GreaterOrEqual:
    return script_bool(!script_val_less(eval(ctx, args[0]), eval(ctx, args[1])));
  case ScriptIntrinsic_LogicAnd:
    return script_bool(script_truthy(eval(ctx, args[0])) && script_truthy(eval(ctx, args[1])));
  case ScriptIntrinsic_LogicOr:
    return script_bool(script_truthy(eval(ctx, args[0])) || script_truthy(eval(ctx, args[1])));
  case ScriptIntrinsic_NullCoalescing: {
    const ScriptVal a = eval(ctx, args[0]);
    return script_val_has(a) ? a : eval(ctx, args[1]);
  }
  case ScriptIntrinsic_Add:
    return script_val_add(eval(ctx, args[0]), eval(ctx, args[1]));
  case ScriptIntrinsic_Sub:
    return script_val_sub(eval(ctx, args[0]), eval(ctx, args[1]));
  case ScriptIntrinsic_Mul:
    return script_val_mul(eval(ctx, args[0]), eval(ctx, args[1]));
  case ScriptIntrinsic_Div:
    return script_val_div(eval(ctx, args[0]), eval(ctx, args[1]));
  case ScriptIntrinsic_Mod:
    return script_val_mod(eval(ctx, args[0]), eval(ctx, args[1]));
  case ScriptIntrinsic_Distance:
    return script_val_dist(eval(ctx, args[0]), eval(ctx, args[1]));
  case ScriptIntrinsic_Angle:
    return script_val_angle(eval(ctx, args[0]), eval(ctx, args[1]));
  case ScriptIntrinsic_RandomBetween:
    return script_val_random_between(eval(ctx, args[0]), eval(ctx, args[1]));
  case ScriptIntrinsic_ComposeVector3:
    return script_val_compose_vector3(eval(ctx, args[0]), eval(ctx, args[1]), eval(ctx, args[2]));
  case ScriptIntrinsic_If:
    return script_truthy(eval(ctx, args[0])) ? eval(ctx, args[1]) : eval(ctx, args[2]);
  case ScriptIntrinsic_Count:
    break;
  }
  diag_assert_fail("Invalid intrinsic");
  UNREACHABLE
}

INLINE_HINT static ScriptVal eval_block(ScriptEvalContext* ctx, const ScriptExprBlock* expr) {
  const ScriptExpr* exprs = expr_set_data(ctx, expr->exprSet);

  // NOTE: Blocks need at least one expression.
  ScriptVal ret;
  for (u32 i = 0; i != expr->exprCount; ++i) {
    ret = eval(ctx, exprs[i]);
  }
  return ret;
}

NO_INLINE_HINT static ScriptVal eval(ScriptEvalContext* ctx, const ScriptExpr expr) {
  switch (script_expr_type(ctx->doc, expr)) {
  case ScriptExprType_Value:
    return eval_value(ctx, &expr_data(ctx, expr)->data_value);
  case ScriptExprType_VarLoad:
    return eval_var_load(ctx, &expr_data(ctx, expr)->data_var_load);
  case ScriptExprType_VarStore:
    return eval_var_store(ctx, &expr_data(ctx, expr)->data_var_store);
  case ScriptExprType_MemLoad:
    return eval_mem_load(ctx, &expr_data(ctx, expr)->data_mem_load);
  case ScriptExprType_MemStore:
    return eval_mem_store(ctx, &expr_data(ctx, expr)->data_mem_store);
  case ScriptExprType_Intrinsic:
    return eval_intr(ctx, &expr_data(ctx, expr)->data_intrinsic);
  case ScriptExprType_Block:
    return eval_block(ctx, &expr_data(ctx, expr)->data_block);
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
