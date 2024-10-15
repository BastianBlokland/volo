#include "script_eval.h"
#include "script_optimize.h"

#include "doc_internal.h"

static bool expr_is_intrinsic(ScriptDoc* doc, const ScriptExpr e, const ScriptIntrinsic intr) {
  if (expr_kind(doc, e) != ScriptExprKind_Intrinsic) {
    return false;
  }
  const ScriptExprIntrinsic* data = &expr_data(doc, e)->intrinsic;
  return data->intrinsic == intr;
}

static bool expr_is_mem_load(ScriptDoc* doc, const ScriptExpr e, const StringHash key) {
  if (expr_kind(doc, e) != ScriptExprKind_MemLoad) {
    return false;
  }
  const ScriptExprMemLoad* data = &expr_data(doc, e)->mem_load;
  return data->key == key;
}

/**
 * Pre-evaluate static expressions.
 * Example: '1 + 2' -> '3'.
 */
static ScriptExpr rewriter_static_eval(void* ctx, ScriptDoc* doc, const ScriptExpr e) {
  (void)ctx;
  if (script_expr_kind(doc, e) == ScriptExprKind_Value) {
    return e; // Already a value; no need to pre-evaluate.
  }
  if (script_expr_static(doc, e)) {
    const ScriptEvalResult evalRes = script_eval(doc, e, null, null, null);
    if (!script_panic_valid(&evalRes.panic)) {
      return script_add_value(doc, script_expr_range(doc, e), evalRes.val);
    }
  }
  return e; // Not possible to pre-evaluate.
}

/**
 * Rewrite null-coalescing memory stores to avoid re-storing the same value.
 * Example: '$a = $a ?? 42' -> '$a ?? ($a = 42)'
 * Example: '$a ??= 42'     -> '$a ?? ($a = 42)'
 */
static ScriptExpr rewriter_null_coalescing_store(void* ctx, ScriptDoc* doc, const ScriptExpr e) {
  (void)ctx;
  switch (script_expr_kind(doc, e)) {
  case ScriptExprKind_MemStore: {
    const ScriptRange storeRange = script_expr_range(doc, e);
    const ScriptExpr  storeVal   = expr_data(doc, e)->mem_store.val;
    const StringHash  storeKey   = expr_data(doc, e)->mem_store.key;
    if (!expr_is_intrinsic(doc, storeVal, ScriptIntrinsic_NullCoalescing)) {
      return e; // Not a null-coalescing store.
    }
    const ScriptExprIntrinsic* valData = &expr_data(doc, storeVal)->intrinsic;
    const ScriptExpr           valLhs  = expr_set_data(doc, valData->argSet)[0];
    const ScriptExpr           valRhs  = expr_set_data(doc, valData->argSet)[1];
    if (!expr_is_mem_load(doc, valLhs, storeKey)) {
      return e; // Not a null-coalescing store.
    }
    const ScriptExpr newArgs[] = {
        script_add_mem_load(doc, storeRange, storeKey),
        script_add_mem_store(doc, storeRange, storeKey, valRhs),
    };
    return script_add_intrinsic(doc, storeRange, ScriptIntrinsic_NullCoalescing, newArgs);
  }
  default:
    return e; // Not a null-coalescing store.
  }
}

ScriptExpr script_optimize(ScriptDoc* doc, ScriptExpr e) {
  e = script_expr_rewrite(doc, e, null, rewriter_null_coalescing_store);
  e = script_expr_rewrite(doc, e, null, rewriter_static_eval);
  return e;
}
