#include "core/array.h"
#include "core/diag.h"
#include "script/eval.h"
#include "script/intrinsic.h"
#include "script/optimize.h"

#include "doc.h"
#include "val.h"

static bool expr_is_mem_load(ScriptDoc* d, const ScriptExpr e, const StringHash key) {
  if (expr_kind(d, e) != ScriptExprKind_MemLoad) {
    return false;
  }
  const ScriptExprMemLoad* data = &expr_data(d, e)->mem_load;
  return data->key == key;
}

static ScriptExpr expr_intrinsic_arg(ScriptDoc* d, const ScriptExpr e, const u32 argIndex) {
  diag_assert(expr_kind(d, e) == ScriptExprKind_Intrinsic);
  diag_assert(argIndex < script_intrinsic_arg_count(expr_data(d, e)->intrinsic.intrinsic));
  return expr_set_data(d, expr_data(d, e)->intrinsic.argSet)[argIndex];
}

#define opt_prune_max_vars 32

typedef struct {
  ScriptVarId   id;
  ScriptScopeId scope;
  ScriptExpr    val;
} OptPruneEntry;

typedef struct {
  OptPruneEntry vars[opt_prune_max_vars];
} OptPruneContext;

static OptPruneEntry*
opt_prune_find(OptPruneContext* ctx, const ScriptValId var, const ScriptScopeId scope) {
  for (u32 i = 0; i != opt_prune_max_vars; ++i) {
    if (ctx->vars[i].id == var && ctx->vars[i].scope == scope) {
      return &ctx->vars[i];
    }
  }
  return null;
}

static void opt_prune_register_store(
    OptPruneContext* ctx, const ScriptDoc* d, const ScriptExprVarStore* store) {

  OptPruneEntry* existing = opt_prune_find(ctx, store->var, store->scope);
  if (existing) {
    // Second store for the same variable; not eligible for pruning.
    existing->id = script_var_sentinel;
    return;
  }

  // Validate that the value expression is safe to move (no side-effects).
  if (!script_expr_static(d, store->val)) {
    return; // Value not static; not eligible for pruning.
  }

  // Register the prune candidate.
  for (u32 i = 0; i != opt_prune_max_vars; ++i) {
    if (sentinel_check(ctx->vars[i].id)) {
      ctx->vars[i].id    = store->var;
      ctx->vars[i].scope = store->scope;
      ctx->vars[i].val   = store->val;
      break;
    }
  }
}

static void opt_prune_collect(void* ctx, const ScriptDoc* d, const ScriptExpr e) {
  OptPruneContext* pruneCtx = ctx;
  if (script_expr_kind(d, e) == ScriptExprKind_VarStore) {
    opt_prune_register_store(pruneCtx, d, &expr_data(d, e)->var_store);
  }
}

static ScriptExpr opt_prune_rewriter(void* ctx, ScriptDoc* d, const ScriptExpr e) {
  OptPruneContext* pruneCtx = ctx;
  switch (script_expr_kind(d, e)) {
  case ScriptExprKind_VarStore: {
    const ScriptExprVarStore* data     = &expr_data(d, e)->var_store;
    OptPruneEntry*            pruneVar = opt_prune_find(pruneCtx, data->var, data->scope);
    if (pruneVar) {
      diag_assert(pruneVar->val == data->val);
      return pruneVar->val;
    }
  } break;
  case ScriptExprKind_VarLoad: {
    const ScriptExprVarLoad* data     = &expr_data(d, e)->var_load;
    OptPruneEntry*           pruneVar = opt_prune_find(pruneCtx, data->var, data->scope);
    if (pruneVar) {
      return pruneVar->val;
    }
  } break;
  default:
    break;
  }
  return e; // Cannot be pruned.
}

/**
 * Remove unnecessary (static value) variables.
 * Example: 'var a = 1; var b = a + 2' -> '1; 1 + 2'.
 */
static ScriptExpr opt_prune(ScriptDoc* d, const ScriptExpr e) {
  OptPruneContext pruneCtx = {0};
  mem_set(array_mem(pruneCtx.vars), 0xFF);

  script_expr_visit(d, e, &pruneCtx, opt_prune_collect);

  return script_expr_rewrite(d, e, &pruneCtx, opt_prune_rewriter);
}

/**
 * Pre-evaluate static control-flow.
 * Example: 'true ? $a : $b' -> '$a'.
 * Example: 'false ? $a : $b' -> '$b'.
 */
static ScriptExpr opt_static_flow_rewriter(void* ctx, ScriptDoc* d, const ScriptExpr e) {
  (void)ctx;
  if (script_expr_kind(d, e) != ScriptExprKind_Intrinsic) {
    return e;
  }
  const ScriptExprIntrinsic* data  = &expr_data(d, e)->intrinsic;
  const ScriptExpr*          exprs = expr_set_data(d, data->argSet);
  switch (data->intrinsic) {
  case ScriptIntrinsic_Select: {
    if (script_expr_static(d, exprs[0])) {
      const bool truthy = script_truthy(script_expr_static_val(d, exprs[0]));
      return opt_static_flow_rewriter(ctx, d, truthy ? exprs[1] : exprs[2]);
    }
  } break;
  case ScriptIntrinsic_NullCoalescing: {
    if (script_expr_static(d, exprs[0])) {
      const bool nonNull = script_non_null(script_expr_static_val(d, exprs[0]));
      return opt_static_flow_rewriter(ctx, d, nonNull ? exprs[0] : exprs[1]);
    }
  } break;
  default:
    break;
  }
  return e;
}

/**
 * Pre-evaluate static expressions.
 * Example: '1 + 2' -> '3'.
 */
static ScriptExpr opt_static_eval_rewriter(void* ctx, ScriptDoc* d, const ScriptExpr e) {
  (void)ctx;
  if (script_expr_kind(d, e) == ScriptExprKind_Value) {
    return e; // Already a value; no need to pre-evaluate.
  }
  if (script_expr_static(d, e)) {
    const ScriptEvalResult evalRes = script_eval(d, null, e, null, null, null);
    if (!evalRes.panic.kind) {
      return script_add_value(d, script_expr_range(d, e), evalRes.val);
    }
  }
  return e; // Not possible to pre-evaluate.
}

/**
 * Rewrite null-coalescing memory stores to avoid re-storing the same value.
 * Example: '$a = $a ?? 42' -> '$a ?? ($a = 42)'
 * Example: '$a ??= 42'     -> '$a ?? ($a = 42)'
 */
static ScriptExpr opt_null_coalescing_store_rewriter(void* ctx, ScriptDoc* d, const ScriptExpr e) {
  (void)ctx;
  if (script_expr_kind(d, e) == ScriptExprKind_MemStore) {
    const ScriptRange storeRange = script_expr_range(d, e);
    const ScriptExpr  storeVal   = expr_data(d, e)->mem_store.val;
    const StringHash  storeKey   = expr_data(d, e)->mem_store.key;
    if (!script_expr_is_intrinsic(d, storeVal, ScriptIntrinsic_NullCoalescing)) {
      return e; // Not a null-coalescing store.
    }
    if (!expr_is_mem_load(d, expr_intrinsic_arg(d, storeVal, 0), storeKey)) {
      return e; // Not a null-coalescing store.
    }
    const ScriptExpr newArgs[] = {
        script_add_mem_load(d, storeRange, storeKey),
        script_add_mem_store(d, storeRange, storeKey, expr_intrinsic_arg(d, storeVal, 1)),
    };
    return script_add_intrinsic(d, storeRange, ScriptIntrinsic_NullCoalescing, newArgs);
  }
  return e; // Not a null-coalescing store.
}

/**
 * Optimize dynamic mem_load / mem_store using static keys.
 * Example: 'mem_load("hello")' -> '$hello'.
 * Example: 'mem_store("hello", 42)' -> '$hello = 42'.
 */
static ScriptExpr opt_static_mem_access(void* ctx, ScriptDoc* d, const ScriptExpr e) {
  (void)ctx;
  // Rewrite dynamic-mem-load intrinsics with a static key expr to non-dynamic mem loads.
  if (script_expr_is_intrinsic(d, e, ScriptIntrinsic_MemLoadDynamic)) {
    const ScriptExpr keyExpr = expr_intrinsic_arg(d, e, 0);
    if (script_expr_static(d, keyExpr)) {
      const ScriptVal keyVal = script_expr_static_val(d, keyExpr);
      if (script_type(keyVal) == ScriptType_Str) {
        return script_add_mem_load(d, script_expr_range(d, e), val_as_str(keyVal));
      }
    }
  }
  // Rewrite dynamic-mem-store intrinsics with a static key expr to non-dynamic mem stores.
  if (script_expr_is_intrinsic(d, e, ScriptIntrinsic_MemStoreDynamic)) {
    const ScriptExpr keyExpr = expr_intrinsic_arg(d, e, 0);
    if (script_expr_static(d, keyExpr)) {
      const ScriptVal keyVal = script_expr_static_val(d, keyExpr);
      if (script_type(keyVal) == ScriptType_Str) {
        const ScriptExpr valExpr    = expr_intrinsic_arg(d, e, 1);
        const ScriptExpr newValExpr = script_expr_rewrite(d, valExpr, null, opt_static_mem_access);
        return script_add_mem_store(d, script_expr_range(d, e), val_as_str(keyVal), newValExpr);
      }
    }
  }
  return e; // Not optimizable.
}

/**
 * Shake any expressions without side-effects where the value is not used.
 * Example: '0; 1; 42' -> '42'
 * Example: '1 + 2; 42' -> '42'
 */
static ScriptExpr opt_shake_rewriter(void* ctx, ScriptDoc* d, const ScriptExpr e) {
  (void)ctx;
  if (script_expr_kind(d, e) == ScriptExprKind_Block) {
    const u32           exprCount = expr_data(d, e)->block.exprCount;
    const ScriptExprSet exprSet   = expr_data(d, e)->block.exprSet;

    ScriptExpr* newExprs     = mem_stack(sizeof(ScriptExpr) * exprCount).ptr;
    u32         newExprCount = 0;
    for (u32 i = 0; i != exprCount; ++i) {
      const ScriptExpr child = expr_set_data(d, exprSet)[i];
      const bool       last  = i == (exprCount - 1);
      if (script_expr_static(d, child) && !last) {
        continue;
      }
      newExprs[newExprCount++] = script_expr_rewrite(d, child, null, opt_shake_rewriter);
    }

    diag_assert(newExprCount);
    if (newExprCount == 1) {
      return newExprs[0];
    }
    return script_add_block(d, script_expr_range(d, e), newExprs, newExprCount);
  }
  return e; // Not shakable.
}

ScriptExpr script_optimize(ScriptDoc* d, ScriptExpr e) {
  e = opt_prune(d, e);
  e = script_expr_rewrite(d, e, null, opt_null_coalescing_store_rewriter);
  e = script_expr_rewrite(d, e, null, opt_static_flow_rewriter);
  e = script_expr_rewrite(d, e, null, opt_static_eval_rewriter);
  e = script_expr_rewrite(d, e, null, opt_static_mem_access);
  e = script_expr_rewrite(d, e, null, opt_shake_rewriter);
  return e;
}
