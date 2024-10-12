#include "core_annotation.h"
#include "core_diag.h"
#include "script_binder.h"
#include "script_error.h"
#include "script_eval.h"
#include "script_mem.h"
#include "script_val.h"

#include "doc_internal.h"
#include "val_internal.h"

#define script_executed_exprs_max 25000

typedef enum {
  ScriptEvalSignal_None     = 0,
  ScriptEvalSignal_Continue = 1 << 0,
  ScriptEvalSignal_Break    = 1 << 1,
  ScriptEvalSignal_Return   = 1 << 2,
  ScriptEvalSignal_Panic    = 1 << 3,
} ScriptEvalSignal;

typedef struct {
  const ScriptDoc*    doc;
  ScriptMem*          m;
  const ScriptBinder* binder;
  void*               bindCtx;
  ScriptEvalSignal    signal;
  ScriptPanic         panic;
  u32                 executedExprs;
  ScriptVal           vars[script_var_count];
} ScriptEvalContext;

static ScriptVal eval_expr(ScriptEvalContext*, ScriptExpr);

INLINE_HINT static ScriptVal eval_value(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprValue* data = &expr_data(ctx->doc, e)->value;
  return dynarray_begin_t(&ctx->doc->values, ScriptVal)[data->valId];
}

INLINE_HINT static ScriptVal eval_var_load(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprVarLoad* data = &expr_data(ctx->doc, e)->var_load;
  return ctx->vars[data->var];
}

INLINE_HINT static ScriptVal eval_var_store(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprVarStore* data = &expr_data(ctx->doc, e)->var_store;
  const ScriptVal           val  = eval_expr(ctx, data->val);
  if (LIKELY(!ctx->signal)) {
    ctx->vars[data->var] = val;
  }
  return val;
}

INLINE_HINT static ScriptVal eval_mem_load(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprMemLoad* data = &expr_data(ctx->doc, e)->mem_load;
  return script_mem_load(ctx->m, data->key);
}

INLINE_HINT static ScriptVal eval_mem_store(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprMemStore* data = &expr_data(ctx->doc, e)->mem_store;
  const ScriptVal           val  = eval_expr(ctx, data->val);
  if (LIKELY(!ctx->signal)) {
    script_mem_store(ctx->m, data->key, val);
  }
  return val;
}

INLINE_HINT static ScriptVal eval_intr(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprIntrinsic* data = &expr_data(ctx->doc, e)->intrinsic;
  const ScriptExpr*          args = expr_set_data(ctx->doc, data->argSet);

#define EVAL_ARG_WITH_INTERRUPT(_NUM_)                                                             \
  const ScriptVal arg##_NUM_ = eval_expr(ctx, args[_NUM_]);                                        \
  if (UNLIKELY(ctx->signal)) {                                                                     \
    return arg##_NUM_;                                                                             \
  }

  switch (data->intrinsic) {
  case ScriptIntrinsic_Continue:
    ctx->signal |= ScriptEvalSignal_Continue;
    return val_null();
  case ScriptIntrinsic_Break:
    ctx->signal |= ScriptEvalSignal_Break;
    return val_null();
  case ScriptIntrinsic_Return: {
    const ScriptVal ret = eval_expr(ctx, args[0]);
    ctx->signal |= ScriptEvalSignal_Return;
    return ret;
  }
  case ScriptIntrinsic_Type:
    return script_val_type(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Hash:
    return script_val_hash(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Assert: {
    if (script_falsy(eval_expr(ctx, args[0]))) {
      ctx->panic = (ScriptPanic){
          .kind  = ScriptPanic_AssertionFailed,
          .range = script_expr_range(ctx->doc, e),
      };
      ctx->signal |= ScriptEvalSignal_Panic;
    }
    return val_null();
  }
  case ScriptIntrinsic_MemLoadDynamic: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return val_type(arg0) == ScriptType_Str ? script_mem_load(ctx->m, val_as_str(arg0))
                                            : val_null();
  }
  case ScriptIntrinsic_MemStoreDynamic: {
    EVAL_ARG_WITH_INTERRUPT(0);
    EVAL_ARG_WITH_INTERRUPT(1);
    if (val_type(arg0) == ScriptType_Str) {
      script_mem_store(ctx->m, val_as_str(arg0), arg1);
      return arg1;
    }
    return val_null();
  }
  case ScriptIntrinsic_Select: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_truthy(arg0) ? eval_expr(ctx, args[1]) : eval_expr(ctx, args[2]);
  }
  case ScriptIntrinsic_NullCoalescing: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_has(arg0) ? arg0 : eval_expr(ctx, args[1]);
  }
  case ScriptIntrinsic_LogicAnd: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return val_bool(script_truthy(arg0) && script_truthy(eval_expr(ctx, args[1])));
  }
  case ScriptIntrinsic_LogicOr: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return val_bool(script_truthy(arg0) || script_truthy(eval_expr(ctx, args[1])));
  }
  case ScriptIntrinsic_Loop: {
    EVAL_ARG_WITH_INTERRUPT(0); // Setup.
    ScriptVal ret = val_null();
    for (;;) {
      EVAL_ARG_WITH_INTERRUPT(1); // Condition.
      if (script_falsy(arg1) || UNLIKELY(ctx->signal)) {
        break;
      }
      ret = eval_expr(ctx, args[3]); // Body.
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
    return val_bool(script_val_equal(arg0, eval_expr(ctx, args[1])));
  }
  case ScriptIntrinsic_NotEqual: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return val_bool(!script_val_equal(arg0, eval_expr(ctx, args[1])));
  }
  case ScriptIntrinsic_Less: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return val_bool(script_val_less(arg0, eval_expr(ctx, args[1])));
  }
  case ScriptIntrinsic_LessOrEqual: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return val_bool(!script_val_greater(arg0, eval_expr(ctx, args[1])));
  }
  case ScriptIntrinsic_Greater: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return val_bool(script_val_greater(arg0, eval_expr(ctx, args[1])));
  }
  case ScriptIntrinsic_GreaterOrEqual: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return val_bool(!script_val_less(arg0, eval_expr(ctx, args[1])));
  }
  case ScriptIntrinsic_Add: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_add(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_Sub: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_sub(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_Mul: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_mul(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_Div: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_div(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_Mod: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_mod(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_Negate:
    return script_val_neg(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Invert:
    return script_val_inv(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Distance: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_dist(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_Angle: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_angle(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_Sin:
    return script_val_sin(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Cos:
    return script_val_cos(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Normalize:
    return script_val_norm(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Magnitude:
    return script_val_mag(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Absolute:
    return script_val_abs(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_VecX:
    return script_val_vec_x(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_VecY:
    return script_val_vec_y(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_VecZ:
    return script_val_vec_z(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Vec3Compose: {
    EVAL_ARG_WITH_INTERRUPT(0);
    EVAL_ARG_WITH_INTERRUPT(1);
    return script_val_vec3_compose(arg0, arg1, eval_expr(ctx, args[2]));
  }
  case ScriptIntrinsic_QuatFromEuler: {
    EVAL_ARG_WITH_INTERRUPT(0);
    EVAL_ARG_WITH_INTERRUPT(1);
    return script_val_quat_from_euler(arg0, arg1, eval_expr(ctx, args[2]));
  }
  case ScriptIntrinsic_QuatFromAngleAxis: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_quat_from_angle_axis(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_ColorCompose: {
    EVAL_ARG_WITH_INTERRUPT(0);
    EVAL_ARG_WITH_INTERRUPT(1);
    EVAL_ARG_WITH_INTERRUPT(2);
    return script_val_color_compose(arg0, arg1, arg2, eval_expr(ctx, args[3]));
  }
  case ScriptIntrinsic_ColorComposeHsv: {
    EVAL_ARG_WITH_INTERRUPT(0);
    EVAL_ARG_WITH_INTERRUPT(1);
    EVAL_ARG_WITH_INTERRUPT(2);
    return script_val_color_compose_hsv(arg0, arg1, arg2, eval_expr(ctx, args[3]));
  }
  case ScriptIntrinsic_ColorFor:
    return script_val_color_for_val(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Random:
    return script_val_random();
  case ScriptIntrinsic_RandomSphere:
    return script_val_random_sphere();
  case ScriptIntrinsic_RandomCircleXZ:
    return script_val_random_circle_xz();
  case ScriptIntrinsic_RandomBetween: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_random_between(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_RoundDown:
    return script_val_round_down(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_RoundNearest:
    return script_val_round_nearest(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_RoundUp:
    return script_val_round_up(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Clamp: {
    EVAL_ARG_WITH_INTERRUPT(0);
    EVAL_ARG_WITH_INTERRUPT(1);
    return script_val_clamp(arg0, arg1, eval_expr(ctx, args[2]));
  }
  case ScriptIntrinsic_Lerp: {
    EVAL_ARG_WITH_INTERRUPT(0);
    EVAL_ARG_WITH_INTERRUPT(1);
    return script_val_lerp(arg0, arg1, eval_expr(ctx, args[2]));
  }
  case ScriptIntrinsic_Min: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_min(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_Max: {
    EVAL_ARG_WITH_INTERRUPT(0);
    return script_val_max(arg0, eval_expr(ctx, args[1]));
  }
  case ScriptIntrinsic_Perlin3:
    return script_val_perlin3(eval_expr(ctx, args[0]));
  case ScriptIntrinsic_Count:
    break;
  }

#undef EVAL_ARG_WITH_INTERRUPT

  diag_assert_fail("Invalid intrinsic");
  UNREACHABLE
}

INLINE_HINT static ScriptVal eval_block(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprBlock* data  = &expr_data(ctx->doc, e)->block;
  const ScriptExpr*      exprs = expr_set_data(ctx->doc, data->exprSet);

  // NOTE: Blocks need at least one expression.
  ScriptVal ret;
  for (u32 i = 0; i != data->exprCount; ++i) {
    ret = eval_expr(ctx, exprs[i]);
    if (UNLIKELY(ctx->signal)) {
      break;
    }
  }
  return ret;
}

INLINE_HINT static ScriptVal eval_extern(ScriptEvalContext* ctx, const ScriptExpr e) {
  const ScriptExprExtern* data      = &expr_data(ctx->doc, e)->extern_;
  const ScriptExpr*       argExprs  = expr_set_data(ctx->doc, data->argSet);
  ScriptVal*              argValues = mem_stack(sizeof(ScriptVal) * data->argCount).ptr;
  for (u16 i = 0; i != data->argCount; ++i) {
    argValues[i] = eval_expr(ctx, argExprs[i]);
    if (UNLIKELY(ctx->signal)) {
      return val_null();
    }
  }
  const ScriptArgs args = {.values = argValues, .count = data->argCount};
  ScriptError      err  = {0};
  const ScriptVal  ret  = script_binder_exec(ctx->binder, data->func, ctx->bindCtx, args, &err);
  if (UNLIKELY(err.kind)) {
    const ScriptExpr errExpr = err.argIndex < data->argCount ? argExprs[err.argIndex] : e;
    ctx->panic               = (ScriptPanic){
        .kind  = script_error_to_panic(err.kind),
        .range = script_expr_range(ctx->doc, errExpr),
    };
    ctx->signal |= ScriptEvalSignal_Panic;
  }
  return ret;
}

NO_INLINE_HINT static ScriptVal eval_expr(ScriptEvalContext* ctx, const ScriptExpr e) {
  if (UNLIKELY(ctx->executedExprs++ == script_executed_exprs_max)) {
    ctx->panic = (ScriptPanic){
        .kind  = ScriptPanic_ExecutionLimitExceeded,
        .range = script_expr_range(ctx->doc, e),
    };
    ctx->signal |= ScriptEvalSignal_Panic;
    return val_null();
  }
  switch (expr_kind(ctx->doc, e)) {
  case ScriptExprKind_Value:
    return eval_value(ctx, e);
  case ScriptExprKind_VarLoad:
    return eval_var_load(ctx, e);
  case ScriptExprKind_VarStore:
    return eval_var_store(ctx, e);
  case ScriptExprKind_MemLoad:
    return eval_mem_load(ctx, e);
  case ScriptExprKind_MemStore:
    return eval_mem_store(ctx, e);
  case ScriptExprKind_Intrinsic:
    return eval_intr(ctx, e);
  case ScriptExprKind_Block:
    return eval_block(ctx, e);
  case ScriptExprKind_Extern:
    return eval_extern(ctx, e);
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

ScriptEvalResult script_eval(
    const ScriptDoc*    doc,
    const ScriptExpr    expr,
    ScriptMem*          m,
    const ScriptBinder* binder,
    void*               bindCtx) {
  if (binder) {
    diag_assert_msg(script_binder_hash(binder) == doc->binderHash, "Incompatible binder");
  }
  ScriptEvalContext ctx = {
      .doc     = doc,
      .m       = m,
      .binder  = binder,
      .bindCtx = bindCtx,
  };

  ScriptEvalResult res;
  res.val           = eval_expr(&ctx, expr);
  res.panic         = ctx.panic;
  res.executedExprs = ctx.executedExprs;

  diag_assert(((ctx.signal & ScriptEvalSignal_Panic) != 0) == script_panic_valid(&ctx.panic));
  diag_assert(!(ctx.signal & ScriptEvalSignal_Break));
  diag_assert(!(ctx.signal & ScriptEvalSignal_Continue));

  return res;
}
