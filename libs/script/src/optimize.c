#include "core_diag.h"
#include "script_eval.h"
#include "script_optimize.h"

#include "doc_internal.h"

static bool expr_is_intrinsic(ScriptDoc* d, const ScriptExpr e, const ScriptIntrinsic intr) {
  if (expr_kind(d, e) != ScriptExprKind_Intrinsic) {
    return false;
  }
  const ScriptExprIntrinsic* data = &expr_data(d, e)->intrinsic;
  return data->intrinsic == intr;
}

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
    const ScriptEvalResult evalRes = script_eval(d, e, null, null, null);
    if (!script_panic_valid(&evalRes.panic)) {
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
  switch (script_expr_kind(d, e)) {
  case ScriptExprKind_MemStore: {
    const ScriptRange storeRange = script_expr_range(d, e);
    const ScriptExpr  storeVal   = expr_data(d, e)->mem_store.val;
    const StringHash  storeKey   = expr_data(d, e)->mem_store.key;
    if (!expr_is_intrinsic(d, storeVal, ScriptIntrinsic_NullCoalescing)) {
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
  default:
    return e; // Not a null-coalescing store.
  }
}

ScriptExpr script_optimize(ScriptDoc* d, ScriptExpr e) {
  e = script_expr_rewrite(d, e, null, opt_null_coalescing_store_rewriter);
  e = script_expr_rewrite(d, e, null, opt_static_eval_rewriter);
  return e;
}
