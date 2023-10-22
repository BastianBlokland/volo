#include "core_annotation.h"
#include "core_diag.h"
#include "script_binder.h"
#include "script_eval.h"
#include "script_mem.h"
#include "script_val.h"

#include "doc_internal.h"

#define script_executed_exprs_max 10000

typedef enum {
  ScriptEvalSignal_None     = 0,
  ScriptEvalSignal_Continue = 1 << 0,
  ScriptEvalSignal_Break    = 1 << 1,
  ScriptEvalSignal_Return   = 1 << 2,
  ScriptEvalSignal_Error    = 1 << 3,
} ScriptEvalSignal;

typedef struct {
  const ScriptDoc*    doc;
  ScriptMem*          m;
  const ScriptBinder* binder;
  void*               bindCtx;
  ScriptEvalSignal    signal : 8;
  ScriptErrorRuntime  error : 8;
  ScriptRange         errorRange;
  u32                 executedExprs;
  ScriptVal           vars[script_var_count];
} ScriptEvalContext;

INLINE_HINT static const ScriptExprData* expr_data(ScriptEvalContext* ctx, const ScriptExpr e) {
  return dynarray_begin_t(&ctx->doc->exprData, ScriptExprData) + e;
}

INLINE_HINT static const ScriptExpr* expr_set_data(ScriptEvalContext* ctx, const ScriptExprSet s) {
  return dynarray_begin_t(&ctx->doc->exprSets, ScriptExpr) + s;
}

static ScriptVal eval(ScriptEvalContext*, ScriptExpr);

INLINE_HINT static ScriptVal eval_value(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprValue* data = &expr_data(ctx, e)->data_value;
  return dynarray_begin_t(&ctx->doc->values, ScriptVal)[data->valId];
}

INLINE_HINT static ScriptVal eval_var_load(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprVarLoad* data = &expr_data(ctx, e)->data_var_load;
  return ctx->vars[data->var];
}

INLINE_HINT static ScriptVal eval_var_store(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprVarStore* data = &expr_data(ctx, e)->data_var_store;
  const ScriptVal           val  = eval(ctx, data->val);
  if (LIKELY(!ctx->signal)) {
    ctx->vars[data->var] = val;
  }
  return val;
}

INLINE_HINT static ScriptVal eval_mem_load(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprMemLoad* data = &expr_data(ctx, e)->data_mem_load;
  return script_mem_get(ctx->m, data->key);
}

INLINE_HINT static ScriptVal eval_mem_store(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprMemStore* data = &expr_data(ctx, e)->data_mem_store;
  const ScriptVal           val  = eval(ctx, data->val);
  if (LIKELY(!ctx->signal)) {
    script_mem_set(ctx->m, data->key, val);
  }
  return val;
}

INLINE_HINT static ScriptVal eval_intr(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprIntrinsic* data = &expr_data(ctx, e)->data_intrinsic;
  const ScriptExpr*          args = expr_set_data(ctx, data->argSet);

#define EVAL_ARG_WITH_INTERRUPT(_NUM_)                                                             \
  const ScriptVal arg##_NUM_ = eval(ctx, args[_NUM_]);                                             \
  if (UNLIKELY(ctx->signal)) {                                                                     \
    return arg##_NUM_;                                                                             \
  }

  switch (data->intrinsic) {
  case ScriptIntrinsic_Continue:
    ctx->signal |= ScriptEvalSignal_Continue;
    return script_null();
  case ScriptIntrinsic_Break:
    ctx->signal |= ScriptEvalSignal_Break;
    return script_null();
  case ScriptIntrinsic_Return: {
    const ScriptVal ret = eval(ctx, args[0]);
    ctx->signal |= ScriptEvalSignal_Return;
    return ret;
  }
  case ScriptIntrinsic_Type:
    return script_string(script_val_type_hash(script_type(eval(ctx, args[0]))));
  case ScriptIntrinsic_Assert: {
    if (script_falsy(eval(ctx, args[0]))) {
      ctx->error      = ScriptErrorRuntime_AssertionFailed;
      ctx->errorRange = script_expr_range(ctx->doc, args[0]);
      ctx->signal |= ScriptEvalSignal_Error;
    }
    return script_null();
  }
  case ScriptIntrinsic_Select: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_truthy(arg0) ? eval(ctx, args[1]) : eval(ctx, args[2]);
  }
  case ScriptIntrinsic_NullCoalescing: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_has(arg0) ? arg0 : eval(ctx, args[1]);
  }
  case ScriptIntrinsic_LogicAnd: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_bool(script_truthy(arg0) && script_truthy(eval(ctx, args[1])));
  }
  case ScriptIntrinsic_LogicOr: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_bool(script_truthy(arg0) || script_truthy(eval(ctx, args[1])));
  }
  case ScriptIntrinsic_Loop: {
    EVAL_ARG_WITH_INTERRUPT(0); // Setup.
    ScriptVal ret = script_null();
    for (;;) {
      EVAL_ARG_WITH_INTERRUPT(1); // Condition.
      if (script_falsy(arg1) || UNLIKELY(ctx->signal)) {
        break;
      }
      ret = eval(ctx, args[3]); // Body.
      if (ctx->signal & ScriptEvalSignal_Continue) {
        ctx->signal &= ~ScriptEvalSignal_Continue;
      }
      if (ctx->signal) {
        ctx->signal &= ~ScriptEvalSignal_Break;
        break;
      }
      EVAL_ARG_WITH_INTERRUPT(2); // Increment.
    }
    return ret;
  }
  case ScriptIntrinsic_Equal: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_bool(script_val_equal(arg0, eval(ctx, args[1])));
  }
  case ScriptIntrinsic_NotEqual: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_bool(!script_val_equal(arg0, eval(ctx, args[1])));
  }
  case ScriptIntrinsic_Less: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_bool(script_val_less(arg0, eval(ctx, args[1])));
  }
  case ScriptIntrinsic_LessOrEqual: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_bool(!script_val_greater(arg0, eval(ctx, args[1])));
  }
  case ScriptIntrinsic_Greater: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_bool(script_val_greater(arg0, eval(ctx, args[1])));
  }
  case ScriptIntrinsic_GreaterOrEqual: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_bool(!script_val_less(arg0, eval(ctx, args[1])));
  }
  case ScriptIntrinsic_Add: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_add(arg0, eval(ctx, args[1]));
  }
  case ScriptIntrinsic_Sub: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_sub(arg0, eval(ctx, args[1]));
  }
  case ScriptIntrinsic_Mul: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_mul(arg0, eval(ctx, args[1]));
  }
  case ScriptIntrinsic_Div: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_div(arg0, eval(ctx, args[1]));
  }
  case ScriptIntrinsic_Mod: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_mod(arg0, eval(ctx, args[1]));
  }
  case ScriptIntrinsic_Negate:
    return script_val_neg(eval(ctx, args[0]));
  case ScriptIntrinsic_Invert:
    return script_val_inv(eval(ctx, args[0]));
  case ScriptIntrinsic_Distance: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_dist(arg0, eval(ctx, args[1]));
  }
  case ScriptIntrinsic_Angle: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_angle(arg0, eval(ctx, args[1]));
  }
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
  case ScriptIntrinsic_Vector3Compose: {
    EVAL_ARG_WITH_INTERRUPT(0);
    EVAL_ARG_WITH_INTERRUPT(1);
    return script_val_vector3_compose(arg0, arg1, eval(ctx, args[2]));
  }
  case ScriptIntrinsic_QuatFromEuler: {
    EVAL_ARG_WITH_INTERRUPT(0);
    EVAL_ARG_WITH_INTERRUPT(1);
    return script_val_quat_from_euler(arg0, arg1, eval(ctx, args[2]));
  }
  case ScriptIntrinsic_QuatFromAngleAxis: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_quat_from_angle_axis(arg0, eval(ctx, args[1]));
  }
  case ScriptIntrinsic_Random:
    return script_val_random();
  case ScriptIntrinsic_RandomSphere:
    return script_val_random_sphere();
  case ScriptIntrinsic_RandomCircleXZ:
    return script_val_random_circle_xz();
  case ScriptIntrinsic_RandomBetween: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_random_between(arg0, eval(ctx, args[1]));
  }
  case ScriptIntrinsic_RoundDown:
    return script_val_round_down(eval(ctx, args[0]));
  case ScriptIntrinsic_RoundNearest:
    return script_val_round_nearest(eval(ctx, args[0]));
  case ScriptIntrinsic_RoundUp:
    return script_val_round_up(eval(ctx, args[0]));
  case ScriptIntrinsic_Count:
    break;
  }

#undef EVAL_ARG_WITH_INTERRUPT

  diag_assert_fail("Invalid intrinsic");
  UNREACHABLE
}

INLINE_HINT static ScriptVal eval_block(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprBlock* data  = &expr_data(ctx, e)->data_block;
  const ScriptExpr*      exprs = expr_set_data(ctx, data->exprSet);

  // NOTE: Blocks need at least one expression.
  ScriptVal ret;
  for (u32 i = 0; i != data->exprCount; ++i) {
    ret = eval(ctx, exprs[i]);
    if (UNLIKELY(ctx->signal)) {
      break;
    }
  }
  return ret;
}

INLINE_HINT static ScriptVal eval_extern(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprExtern* data      = &expr_data(ctx, e)->data_extern;
  const ScriptExpr*       argExprs  = expr_set_data(ctx, data->argSet);
  ScriptVal*              argValues = mem_stack(sizeof(ScriptVal) * data->argCount).ptr;
  for (u32 i = 0; i != data->argCount; ++i) {
    argValues[i] = eval(ctx, argExprs[i]);
    if (UNLIKELY(ctx->signal)) {
      return script_null();
    }
  }
  const ScriptArgs args = {.values = argValues, .count = data->argCount};
  return script_binder_exec(ctx->binder, data->func, ctx->bindCtx, args);
}

NO_INLINE_HINT static ScriptVal eval(ScriptEvalContext* ctx, const ScriptExpr e) {
  if (UNLIKELY(ctx->executedExprs++ == script_executed_exprs_max)) {
    ctx->error      = ScriptErrorRuntime_ExecutionLimitExceeded;
    ctx->errorRange = script_expr_range(ctx->doc, e);
    ctx->signal |= ScriptEvalSignal_Error;
    return script_null();
  }
  switch (script_expr_type(ctx->doc, e)) {
  case ScriptExprType_Value:
    return eval_value(ctx, e);
  case ScriptExprType_VarLoad:
    return eval_var_load(ctx, e);
  case ScriptExprType_VarStore:
    return eval_var_store(ctx, e);
  case ScriptExprType_MemLoad:
    return eval_mem_load(ctx, e);
  case ScriptExprType_MemStore:
    return eval_mem_store(ctx, e);
  case ScriptExprType_Intrinsic:
    return eval_intr(ctx, e);
  case ScriptExprType_Block:
    return eval_block(ctx, e);
  case ScriptExprType_Extern:
    return eval_extern(ctx, e);
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Unknown expression type");
  UNREACHABLE
}

static ScriptErrorRuntime script_error_runtime_type(const ScriptEvalContext* ctx) {
  if (UNLIKELY(ctx->signal & ScriptEvalSignal_Error)) {
    diag_assert_msg(ctx->error, "Invalid error signal");
    return ctx->error;
  }
  if (ctx->signal == ScriptEvalSignal_Return) {
    return ScriptErrorRuntime_None;
  }
  diag_assert_msg(!ctx->signal, "Unhandled signal");
  return ScriptErrorRuntime_None;
}

ScriptEvalResult script_eval(
    const ScriptDoc*    doc,
    ScriptMem*          m,
    const ScriptExpr    expr,
    const ScriptBinder* binder,
    void*               bindCtx) {
  if (binder) {
    diag_assert_msg(script_binder_sig(binder) == doc->binderSignature, "Incompatible binder");
  }
  ScriptEvalContext ctx = {
      .doc     = doc,
      .m       = m,
      .binder  = binder,
      .bindCtx = bindCtx,
  };

  ScriptEvalResult res;
  res.val           = eval(&ctx, expr);
  res.error         = script_error_runtime_type(&ctx);
  res.errorRange    = ctx.errorRange;
  res.executedExprs = ctx.executedExprs;
  return res;
}

ScriptEvalResult
script_eval_readonly(const ScriptDoc* doc, const ScriptMem* m, const ScriptExpr expr) {
  diag_assert(script_expr_readonly(doc, expr));
  ScriptEvalContext ctx = {
      .doc = doc,
      .m   = (ScriptMem*)m, // NOTE: Safe as long as the readonly invariant is maintained.
  };

  ScriptEvalResult res;
  res.val           = eval(&ctx, expr);
  res.error         = script_error_runtime_type(&ctx);
  res.errorRange    = ctx.errorRange;
  res.executedExprs = ctx.executedExprs;
  return res;
}
