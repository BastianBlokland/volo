#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_eval.h"
#include "script_pos.h"

#include "doc_internal.h"

typedef enum {
  ScriptExprFlags_None          = 0,
  ScriptExprFlags_ValidateRange = 1 << 0,
} ScriptExprFlags;

static ScriptExpr doc_expr_add(
    ScriptDoc* doc, const ScriptRange range, const ScriptExprKind kind, const ScriptExprData data) {
  const ScriptExpr expr                            = (ScriptExpr)doc->exprData.size;
  *dynarray_push_t(&doc->exprData, ScriptExprData) = data;
  *dynarray_push_t(&doc->exprKinds, u8)            = (u8)kind;
  *dynarray_push_t(&doc->exprRanges, ScriptRange)  = range;
  return expr;
}

static ScriptValId doc_val_add(ScriptDoc* doc, const ScriptVal val) {
  // Check if there is an existing identical value.
  ScriptValId id = 0;
  for (; id != doc->values.size; ++id) {
    if (script_val_equal(val, dynarray_begin_t(&doc->values, ScriptVal)[id])) {
      return id;
    }
  }
  // If not: Register a new value
  *dynarray_push_t(&doc->values, ScriptVal) = val;
  return id;
}

static ScriptVal doc_val_data(const ScriptDoc* doc, const ScriptValId id) {
  diag_assert_msg(id < doc->values.size, "Out of bounds ScriptValId");
  return dynarray_begin_t(&doc->values, ScriptVal)[id];
}

static ScriptExprSet doc_expr_set_add(ScriptDoc* doc, const ScriptExpr exprs[], const u32 count) {
  const ScriptExprSet set = (ScriptExprSet)doc->exprSets.size;
  mem_cpy(dynarray_push(&doc->exprSets, count), mem_create(exprs, sizeof(ScriptExpr) * count));
  return set;
}

static void doc_validate_subrange(
    MAYBE_UNUSED const ScriptDoc*  doc,
    MAYBE_UNUSED const ScriptRange range,
    MAYBE_UNUSED const ScriptExpr  expr) {
#ifndef VOLO_FAST
  const ScriptRange exprRange = script_expr_range(doc, expr);
  if (!sentinel_check(exprRange.start) && !sentinel_check(exprRange.end)) {
    diag_assert_msg(
        script_range_subrange(range, exprRange),
        "Child expression range is not a sub-range of its parent");
  }
#endif
}

static void doc_validate_subrange_set(
    MAYBE_UNUSED const ScriptDoc*    doc,
    MAYBE_UNUSED const ScriptRange   range,
    MAYBE_UNUSED const ScriptExprSet set,
    MAYBE_UNUSED const u32           count) {
#ifndef VOLO_FAST
  diag_assert_msg(!count || set < doc->exprSets.size, "Out of bounds ScriptExprSet");
  const ScriptExpr* exprs = expr_set_data(doc, set);
  for (u32 i = 0; i != count; ++i) {
    doc_validate_subrange(doc, range, exprs[i]);
  }
#endif
}

static ScriptExpr doc_expr_add_value(
    ScriptDoc* doc, const ScriptRange range, const ScriptVal val, const ScriptExprFlags flags) {
  (void)flags;
  const ScriptValId valId = doc_val_add(doc, val);
  return doc_expr_add(
      doc, range, ScriptExprKind_Value, (ScriptExprData){.value = {.valId = valId}});
}

static ScriptExpr doc_expr_add_var_load(
    ScriptDoc*            doc,
    const ScriptRange     range,
    const ScriptScopeId   scope,
    const ScriptVarId     var,
    const ScriptExprFlags flags) {
  (void)flags;
  diag_assert_msg(var < script_var_count, "Out of bounds script variable");
  return doc_expr_add(
      doc,
      range,
      ScriptExprKind_VarLoad,
      (ScriptExprData){.var_load = {.scope = scope, .var = var}});
}

static ScriptExpr doc_expr_add_var_store(
    ScriptDoc*            doc,
    const ScriptRange     range,
    const ScriptScopeId   scope,
    const ScriptVarId     var,
    const ScriptExpr      val,
    const ScriptExprFlags flags) {
  diag_assert_msg(var < script_var_count, "Out of bounds script variable");
  if (flags & ScriptExprFlags_ValidateRange) {
    doc_validate_subrange(doc, range, val);
  }
  return doc_expr_add(
      doc,
      range,
      ScriptExprKind_VarStore,
      (ScriptExprData){.var_store = {.scope = scope, .var = var, .val = val}});
}

static ScriptExpr doc_expr_add_mem_load(
    ScriptDoc* doc, const ScriptRange range, const StringHash key, const ScriptExprFlags flags) {
  (void)flags;
  diag_assert_msg(key, "Empty key is not valid");
  return doc_expr_add(
      doc, range, ScriptExprKind_MemLoad, (ScriptExprData){.mem_load = {.key = key}});
}

static ScriptExpr doc_expr_add_mem_store(
    ScriptDoc*            doc,
    const ScriptRange     range,
    const StringHash      key,
    const ScriptExpr      val,
    const ScriptExprFlags flags) {
  diag_assert_msg(key, "Empty key is not valid");
  if (flags & ScriptExprFlags_ValidateRange) {
    doc_validate_subrange(doc, range, val);
  }
  return doc_expr_add(
      doc, range, ScriptExprKind_MemStore, (ScriptExprData){.mem_store = {.key = key, .val = val}});
}

static ScriptExpr doc_expr_add_intrinsic(
    ScriptDoc*            doc,
    const ScriptRange     range,
    const ScriptIntrinsic i,
    const ScriptExpr      args[],
    const ScriptExprFlags flags) {
  const u32           argCount = script_intrinsic_arg_count(i);
  const ScriptExprSet argSet   = doc_expr_set_add(doc, args, argCount);
  if (flags & ScriptExprFlags_ValidateRange) {
    doc_validate_subrange_set(doc, range, argSet, argCount);
  }
  return doc_expr_add(
      doc,
      range,
      ScriptExprKind_Intrinsic,
      (ScriptExprData){.intrinsic = {.argSet = argSet, .intrinsic = i}});
}

static ScriptExpr doc_expr_add_block(
    ScriptDoc*            doc,
    const ScriptRange     range,
    const ScriptExpr      exprs[],
    const u32             exprCount,
    const ScriptExprFlags flags) {
  diag_assert_msg(exprCount, "Zero sized blocks are not supported");
  const ScriptExprSet set = doc_expr_set_add(doc, exprs, exprCount);
  if (flags & ScriptExprFlags_ValidateRange) {
    doc_validate_subrange_set(doc, range, set, exprCount);
  }
  return doc_expr_add(
      doc,
      range,
      ScriptExprKind_Block,
      (ScriptExprData){.block = {.exprSet = set, .exprCount = exprCount}});
}

static ScriptExpr doc_expr_add_extern(
    ScriptDoc*             doc,
    const ScriptRange      range,
    const ScriptBinderSlot func,
    const ScriptExpr       args[],
    const u16              argCount,
    const ScriptExprFlags  flags) {
  const ScriptExprSet argSet = doc_expr_set_add(doc, args, argCount);
  if (flags & ScriptExprFlags_ValidateRange) {
    doc_validate_subrange_set(doc, range, argSet, argCount);
  }
  return doc_expr_add(
      doc,
      range,
      ScriptExprKind_Extern,
      (ScriptExprData){.extern_ = {.func = func, .argSet = argSet, .argCount = argCount}});
}

ScriptDoc* script_create(Allocator* alloc) {
  ScriptDoc* doc = alloc_alloc_t(alloc, ScriptDoc);

  *doc = (ScriptDoc){
      .exprData   = dynarray_create_t(alloc, ScriptExprData, 64),
      .exprKinds  = dynarray_create_t(alloc, u8, 64),
      .exprRanges = dynarray_create_t(alloc, ScriptRange, 64),
      .exprSets   = dynarray_create_t(alloc, ScriptExpr, 32),
      .values     = dynarray_create_t(alloc, ScriptVal, 32),
      .alloc      = alloc,
  };
  return doc;
}

void script_destroy(ScriptDoc* doc) {
  string_maybe_free(doc->alloc, doc->sourceText);
  dynarray_destroy(&doc->exprData);
  dynarray_destroy(&doc->exprKinds);
  dynarray_destroy(&doc->exprRanges);
  dynarray_destroy(&doc->exprSets);
  dynarray_destroy(&doc->values);
  alloc_free_t(doc->alloc, doc);
}

void script_clear(ScriptDoc* doc) {
  dynarray_clear(&doc->exprData);
  dynarray_clear(&doc->exprKinds);
  dynarray_clear(&doc->exprRanges);
  dynarray_clear(&doc->exprSets);
  dynarray_clear(&doc->values);
}

void script_source_set(ScriptDoc* doc, const String sourceText) {
  if (!string_is_empty(doc->sourceText)) {
    string_maybe_free(doc->alloc, doc->sourceText);
  }
  doc->sourceText = string_maybe_dup(doc->alloc, sourceText);
}

String script_source_get(ScriptDoc* doc) { return doc->sourceText; }

ScriptExpr script_add_value(ScriptDoc* doc, const ScriptRange range, const ScriptVal val) {
  return doc_expr_add_value(doc, range, val, ScriptExprFlags_ValidateRange);
}

ScriptExpr script_add_var_load(
    ScriptDoc* doc, const ScriptRange range, const ScriptScopeId scope, const ScriptVarId var) {
  return doc_expr_add_var_load(doc, range, scope, var, ScriptExprFlags_ValidateRange);
}

ScriptExpr script_add_var_store(
    ScriptDoc*          doc,
    const ScriptRange   range,
    const ScriptScopeId scope,
    const ScriptVarId   var,
    const ScriptExpr    val) {
  return doc_expr_add_var_store(doc, range, scope, var, val, ScriptExprFlags_ValidateRange);
}

ScriptExpr script_add_mem_load(ScriptDoc* doc, const ScriptRange range, const StringHash key) {
  return doc_expr_add_mem_load(doc, range, key, ScriptExprFlags_ValidateRange);
}

ScriptExpr script_add_mem_store(
    ScriptDoc* doc, const ScriptRange range, const StringHash key, const ScriptExpr val) {
  return doc_expr_add_mem_store(doc, range, key, val, ScriptExprFlags_ValidateRange);
}

ScriptExpr script_add_intrinsic(
    ScriptDoc* doc, const ScriptRange range, const ScriptIntrinsic i, const ScriptExpr args[]) {
  return doc_expr_add_intrinsic(doc, range, i, args, ScriptExprFlags_ValidateRange);
}

ScriptExpr script_add_block(
    ScriptDoc* doc, const ScriptRange range, const ScriptExpr exprs[], const u32 exprCount) {
  return doc_expr_add_block(doc, range, exprs, exprCount, ScriptExprFlags_ValidateRange);
}

ScriptExpr script_add_extern(
    ScriptDoc*             doc,
    const ScriptRange      range,
    const ScriptBinderSlot func,
    const ScriptExpr       args[],
    const u16              argCount) {
  return doc_expr_add_extern(doc, range, func, args, argCount, ScriptExprFlags_ValidateRange);
}

ScriptExpr script_add_anon_value(ScriptDoc* doc, const ScriptVal val) {
  return doc_expr_add_value(doc, script_range_sentinel, val, ScriptExprFlags_None);
}

ScriptExpr
script_add_anon_var_load(ScriptDoc* doc, const ScriptScopeId scope, const ScriptVarId var) {
  return doc_expr_add_var_load(doc, script_range_sentinel, scope, var, ScriptExprFlags_None);
}

ScriptExpr script_add_anon_var_store(
    ScriptDoc* doc, const ScriptScopeId scope, const ScriptVarId var, const ScriptExpr val) {
  return doc_expr_add_var_store(doc, script_range_sentinel, scope, var, val, ScriptExprFlags_None);
}

ScriptExpr script_add_anon_mem_load(ScriptDoc* doc, const StringHash key) {
  return doc_expr_add_mem_load(doc, script_range_sentinel, key, ScriptExprFlags_None);
}

ScriptExpr script_add_anon_mem_store(ScriptDoc* doc, const StringHash key, const ScriptExpr val) {
  return doc_expr_add_mem_store(doc, script_range_sentinel, key, val, ScriptExprFlags_None);
}

ScriptExpr
script_add_anon_intrinsic(ScriptDoc* doc, const ScriptIntrinsic i, const ScriptExpr args[]) {
  return doc_expr_add_intrinsic(doc, script_range_sentinel, i, args, ScriptExprFlags_None);
}

u32 script_values_total(const ScriptDoc* doc) { return (u32)doc->values.size; }

ScriptExprKind script_expr_kind(const ScriptDoc* doc, const ScriptExpr expr) {
  diag_assert_msg(expr < doc->exprData.size, "Out of bounds ScriptExpr");
  return expr_kind(doc, expr);
}

ScriptRange script_expr_range(const ScriptDoc* doc, const ScriptExpr expr) {
  diag_assert_msg(expr < doc->exprRanges.size, "Out of bounds ScriptExpr");
  return expr_range(doc, expr);
}

ScriptRangeLineCol script_expr_range_line_col(const ScriptDoc* doc, const ScriptExpr expr) {
  if (string_is_empty(doc->sourceText)) {
    return (ScriptRangeLineCol){0};
  }
  const ScriptRange range = script_expr_range(doc, expr);
  return script_range_to_line_col(doc->sourceText, range);
}

static void script_visitor_static(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  bool* isStatic = ctx;
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_MemLoad:
  case ScriptExprKind_MemStore:
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_VarStore:
  case ScriptExprKind_Extern:
    *isStatic = false;
    return;
  case ScriptExprKind_Intrinsic: {
    if (!script_intrinsic_deterministic(expr_data(doc, expr)->intrinsic.intrinsic)) {
      *isStatic = false;
    }
    return;
  }
  case ScriptExprKind_Value:
  case ScriptExprKind_Block:
    return;
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

bool script_expr_static(const ScriptDoc* doc, const ScriptExpr expr) {
  bool isStatic = true;
  script_expr_visit(doc, expr, &isStatic, script_visitor_static);
  return isStatic;
}

ScriptVal script_expr_static_val(const ScriptDoc* doc, const ScriptExpr expr) {
  if (!script_expr_static(doc, expr)) {
    return script_null();
  }
  const ScriptEvalResult evalRes = script_eval(doc, expr, null, null, null);
  return script_panic_valid(&evalRes.panic) ? script_null() : evalRes.val;
}

bool script_expr_always_truthy(const ScriptDoc* doc, const ScriptExpr expr) {
  if (!script_expr_static(doc, expr)) {
    return false;
  }
  const ScriptEvalResult evalRes = script_eval(doc, expr, null, null, null);
  return !script_panic_valid(&evalRes.panic) && script_truthy(evalRes.val);
}

void script_expr_visit(
    const ScriptDoc* doc, const ScriptExpr expr, void* ctx, ScriptVisitor visitor) {
  /**
   * Visit the expression itself.
   */
  visitor(ctx, doc, expr);

  /**
   * Visit the expression's children.
   */
  const ScriptExprData* data = expr_data(doc, expr);
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_Value:
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_MemLoad:
    return; // No children.
  case ScriptExprKind_VarStore:
    script_expr_visit(doc, data->var_store.val, ctx, visitor);
    return;
  case ScriptExprKind_MemStore:
    script_expr_visit(doc, data->mem_store.val, ctx, visitor);
    return;
  case ScriptExprKind_Intrinsic: {
    const ScriptExpr* args     = expr_set_data(doc, data->intrinsic.argSet);
    const u32         argCount = script_intrinsic_arg_count(data->intrinsic.intrinsic);
    for (u32 i = 0; i != argCount; ++i) {
      script_expr_visit(doc, args[i], ctx, visitor);
    }
    return;
  }
  case ScriptExprKind_Block: {
    const ScriptExpr* exprs = expr_set_data(doc, data->block.exprSet);
    for (u32 i = 0; i != data->block.exprCount; ++i) {
      script_expr_visit(doc, exprs[i], ctx, visitor);
    }
    return;
  }
  case ScriptExprKind_Extern: {
    const ScriptExpr* args = expr_set_data(doc, data->extern_.argSet);
    for (u16 i = 0; i != data->extern_.argCount; ++i) {
      script_expr_visit(doc, args[i], ctx, visitor);
    }
    return;
  }
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

ScriptExpr
script_expr_rewrite(ScriptDoc* doc, const ScriptExpr expr, void* ctx, ScriptRewriter rewriter) {
  const ScriptExpr rewritten = rewriter(ctx, doc, expr);
  if (rewritten != expr) {
    return rewritten;
  }
  const ScriptExprData data  = *expr_data(doc, expr); // Copy as rewrites invalidate pointers.
  const ScriptRange    range = script_expr_range(doc, expr);
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_Value:
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_MemLoad:
    return expr; // No children.
  case ScriptExprKind_VarStore: {
    const ScriptExpr newVal = script_expr_rewrite(doc, data.var_store.val, ctx, rewriter);
    if (newVal == data.var_store.val) {
      return expr; // Not rewritten.
    }
    return doc_expr_add_var_store(doc, range, data.var_store.scope, data.var_store.var, newVal, 0);
  }
  case ScriptExprKind_MemStore: {
    const ScriptExpr newVal = script_expr_rewrite(doc, data.mem_store.val, ctx, rewriter);
    if (newVal == data.mem_store.val) {
      return expr; // Not rewritten.
    }
    return doc_expr_add_mem_store(doc, range, data.mem_store.key, newVal, 0);
  }
  case ScriptExprKind_Intrinsic: {
    const u32   argCount     = script_intrinsic_arg_count(data.intrinsic.intrinsic);
    ScriptExpr* newArgs      = mem_stack(sizeof(ScriptExpr) * argCount).ptr;
    bool        anyRewritten = false;
    for (u32 i = 0; i != argCount; ++i) {
      const ScriptExpr oldArg = expr_set_data(doc, data.intrinsic.argSet)[i];
      newArgs[i]              = script_expr_rewrite(doc, oldArg, ctx, rewriter);
      anyRewritten |= newArgs[i] != oldArg;
    }
    if (!anyRewritten) {
      return expr; // Not rewritten.
    }
    return doc_expr_add_intrinsic(doc, range, data.intrinsic.intrinsic, newArgs, 0);
  }
  case ScriptExprKind_Block: {
    ScriptExpr* newExprs     = mem_stack(sizeof(ScriptExpr) * data.block.exprCount).ptr;
    bool        anyRewritten = false;
    for (u32 i = 0; i != data.block.exprCount; ++i) {
      const ScriptExpr oldExpr = expr_set_data(doc, data.block.exprSet)[i];
      newExprs[i]              = script_expr_rewrite(doc, oldExpr, ctx, rewriter);
      anyRewritten |= newExprs[i] != oldExpr;
    }
    if (!anyRewritten) {
      return expr; // Not rewritten.
    }
    return doc_expr_add_block(doc, range, newExprs, data.block.exprCount, 0);
  }
  case ScriptExprKind_Extern: {
    ScriptExpr* newArgs      = mem_stack(sizeof(ScriptExpr) * data.extern_.argCount).ptr;
    bool        anyRewritten = false;
    for (u32 i = 0; i != data.extern_.argCount; ++i) {
      const ScriptExpr oldArg = expr_set_data(doc, data.extern_.argSet)[i];
      newArgs[i]              = script_expr_rewrite(doc, oldArg, ctx, rewriter);
      anyRewritten |= newArgs[i] != oldArg;
    }
    if (!anyRewritten) {
      return expr; // Not rewritten.
    }
    return doc_expr_add_extern(doc, range, data.extern_.func, newArgs, data.extern_.argCount, 0);
  }
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

ScriptDocSignal script_expr_always_uncaught_signal(const ScriptDoc* doc, const ScriptExpr expr) {
  const ScriptExprData* data = expr_data(doc, expr);
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_Value:
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_MemLoad:
    return ScriptDocSignal_None; // No children.
  case ScriptExprKind_VarStore:
    return script_expr_always_uncaught_signal(doc, data->var_store.val);
  case ScriptExprKind_MemStore:
    return script_expr_always_uncaught_signal(doc, data->mem_store.val);
  case ScriptExprKind_Intrinsic: {
    const ScriptExpr* args = expr_set_data(doc, data->intrinsic.argSet);
    const u32 argCount     = script_intrinsic_arg_count_always_reached(data->intrinsic.intrinsic);
    switch (data->intrinsic.intrinsic) {
    case ScriptIntrinsic_Continue:
      return ScriptDocSignal_Continue;
    case ScriptIntrinsic_Break:
      return ScriptDocSignal_Break;
    case ScriptIntrinsic_Return:
      return script_expr_always_uncaught_signal(doc, args[0]) | ScriptDocSignal_Return;
    case ScriptIntrinsic_Select: {
      ScriptDocSignal sig = script_expr_always_uncaught_signal(doc, args[0]);
      if (sig) {
        return sig;
      }
      if (script_expr_static(doc, args[0])) {
        const ScriptEvalResult res = script_eval(doc, args[0], null, null, null);
        if (!script_panic_valid(&res.panic)) {
          const bool condition = script_truthy(res.val);
          return script_expr_always_uncaught_signal(doc, condition ? args[1] : args[2]);
        }
      }
      return ScriptDocSignal_None;
    }
    default:
      for (u32 i = 0; i != argCount; ++i) {
        const ScriptDocSignal sig = script_expr_always_uncaught_signal(doc, args[i]);
        if (sig) {
          return sig;
        }
      }
    }
    return ScriptDocSignal_None;
  }
  case ScriptExprKind_Block: {
    const ScriptExpr* exprs = expr_set_data(doc, data->block.exprSet);
    for (u32 i = 0; i != data->block.exprCount; ++i) {
      const ScriptDocSignal sig = script_expr_always_uncaught_signal(doc, exprs[i]);
      if (sig) {
        return sig;
      }
    }
    return ScriptDocSignal_None;
  }
  case ScriptExprKind_Extern: {
    const ScriptExpr* args = expr_set_data(doc, data->extern_.argSet);
    for (u16 i = 0; i != data->extern_.argCount; ++i) {
      const ScriptDocSignal sig = script_expr_always_uncaught_signal(doc, args[i]);
      if (sig) {
        return sig;
      }
    }
    return ScriptDocSignal_None;
  }
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

INLINE_HINT static ScriptExpr script_expr_find_var_store(
    const ScriptDoc* doc,
    const ScriptExpr root,
    const ScriptPos  pos,
    void*            ctx,
    const ScriptPred pred) {
  const ScriptExprData* data = expr_data(doc, root);
  if (script_range_contains(script_expr_range(doc, data->var_store.val), pos)) {
    const ScriptExpr res = script_expr_find(doc, data->var_store.val, pos, ctx, pred);
    if (!sentinel_check(res)) {
      return res;
    }
  }
  return (!pred || pred(ctx, doc, root)) ? root : script_expr_sentinel;
}

INLINE_HINT static ScriptExpr script_expr_find_mem_store(
    const ScriptDoc* doc,
    const ScriptExpr root,
    const ScriptPos  pos,
    void*            ctx,
    const ScriptPred pred) {
  const ScriptExprData* data = expr_data(doc, root);
  if (script_range_contains(script_expr_range(doc, data->mem_store.val), pos)) {
    const ScriptExpr res = script_expr_find(doc, data->mem_store.val, pos, ctx, pred);
    if (!sentinel_check(res)) {
      return res;
    }
  }
  return (!pred || pred(ctx, doc, root)) ? root : script_expr_sentinel;
}

INLINE_HINT static ScriptExpr script_expr_find_intrinsic(
    const ScriptDoc* doc,
    const ScriptExpr root,
    const ScriptPos  pos,
    void*            ctx,
    const ScriptPred pred) {
  const ScriptExprData* data     = expr_data(doc, root);
  const ScriptExpr*     args     = expr_set_data(doc, data->intrinsic.argSet);
  const u32             argCount = script_intrinsic_arg_count(data->intrinsic.intrinsic);
  for (u32 i = 0; i != argCount; ++i) {
    if (script_range_contains(script_expr_range(doc, args[i]), pos)) {
      const ScriptExpr res = script_expr_find(doc, args[i], pos, ctx, pred);
      if (!sentinel_check(res)) {
        return res;
      }
      break;
    }
  }
  return (!pred || pred(ctx, doc, root)) ? root : script_expr_sentinel;
}

INLINE_HINT static ScriptExpr script_expr_find_block(
    const ScriptDoc* doc,
    const ScriptExpr root,
    const ScriptPos  pos,
    void*            ctx,
    const ScriptPred pred) {
  const ScriptExprData* data  = expr_data(doc, root);
  const ScriptExpr*     exprs = expr_set_data(doc, data->block.exprSet);
  for (u32 i = 0; i != data->block.exprCount; ++i) {
    if (script_range_contains(script_expr_range(doc, exprs[i]), pos)) {
      const ScriptExpr res = script_expr_find(doc, exprs[i], pos, ctx, pred);
      if (!sentinel_check(res)) {
        return res;
      }
      break;
    }
  }
  return (!pred || pred(ctx, doc, root)) ? root : script_expr_sentinel;
}

INLINE_HINT static ScriptExpr script_expr_find_extern(
    const ScriptDoc* doc,
    const ScriptExpr root,
    const ScriptPos  pos,
    void*            ctx,
    const ScriptPred pred) {
  const ScriptExprData* data = expr_data(doc, root);
  const ScriptExpr*     args = expr_set_data(doc, data->extern_.argSet);
  for (u16 i = 0; i != data->extern_.argCount; ++i) {
    if (script_range_contains(script_expr_range(doc, args[i]), pos)) {
      const ScriptExpr res = script_expr_find(doc, args[i], pos, ctx, pred);
      if (!sentinel_check(res)) {
        return res;
      }
      break;
    }
  }
  return (!pred || pred(ctx, doc, root)) ? root : script_expr_sentinel;
}

ScriptExpr script_expr_find(
    const ScriptDoc* doc,
    const ScriptExpr root,
    const ScriptPos  pos,
    void*            ctx,
    const ScriptPred pred) {
  switch (expr_kind(doc, root)) {
  case ScriptExprKind_VarStore:
    return script_expr_find_var_store(doc, root, pos, ctx, pred);
  case ScriptExprKind_MemStore:
    return script_expr_find_mem_store(doc, root, pos, ctx, pred);
  case ScriptExprKind_Intrinsic:
    return script_expr_find_intrinsic(doc, root, pos, ctx, pred);
  case ScriptExprKind_Block:
    return script_expr_find_block(doc, root, pos, ctx, pred);
  case ScriptExprKind_Extern:
    return script_expr_find_extern(doc, root, pos, ctx, pred);
  case ScriptExprKind_Value:
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_MemLoad:
  case ScriptExprKind_Count:
    break; // No child expressions.
  }
  return (!pred || pred(ctx, doc, root)) ? root : script_expr_sentinel;
}

u32 script_expr_arg_count(const ScriptDoc* doc, const ScriptExpr expr) {
  const ScriptExprData* data = expr_data(doc, expr);
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_Intrinsic:
    return script_intrinsic_arg_count(data->intrinsic.intrinsic);
  case ScriptExprKind_Extern:
    return data->extern_.argCount;
  default:
    return 0;
  }
}

u32 script_expr_arg_index(const ScriptDoc* doc, const ScriptExpr expr, const ScriptPos pos) {
  const ScriptExprData* data = expr_data(doc, expr);
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_Intrinsic: {
    const ScriptExpr* args     = expr_set_data(doc, data->intrinsic.argSet);
    const u32         argCount = script_intrinsic_arg_count(data->intrinsic.intrinsic);
    for (u32 i = 0; i != argCount; ++i) {
      if (pos <= script_expr_range(doc, args[i]).end) {
        return i;
      }
    }
    return sentinel_u32;
  }
  case ScriptExprKind_Extern: {
    const ScriptExpr* args = expr_set_data(doc, data->extern_.argSet);
    for (u16 i = 0; i != data->extern_.argCount; ++i) {
      if (pos <= script_expr_range(doc, args[i]).end) {
        return i;
      }
    }
    return sentinel_u32;
  }
  default:
    return sentinel_u32;
  }
}

String script_expr_kind_str(const ScriptExprKind kind) {
  switch (kind) {
  case ScriptExprKind_Value:
    return string_lit("value");
  case ScriptExprKind_VarLoad:
    return string_lit("var-load");
  case ScriptExprKind_VarStore:
    return string_lit("var-store");
  case ScriptExprKind_MemLoad:
    return string_lit("mem-load");
  case ScriptExprKind_MemStore:
    return string_lit("mem-store");
  case ScriptExprKind_Intrinsic:
    return string_lit("intrinsic");
  case ScriptExprKind_Block:
    return string_lit("block");
  case ScriptExprKind_Extern:
    return string_lit("extern");
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

static void script_expr_write_sep(const u32 indent, DynString* str) {
  dynstring_append_char(str, '\n');
  dynstring_append_chars(str, ' ', indent * 2);
}

static void script_expr_write_child(
    const ScriptDoc* doc, const ScriptExpr expr, const u32 indent, DynString* str) {
  script_expr_write_sep(indent, str);
  script_expr_write(doc, expr, indent, str);
}

void script_expr_write(
    const ScriptDoc* doc, const ScriptExpr expr, const u32 indent, DynString* str) {
  const ScriptExprData* data = expr_data(doc, expr);
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_Value:
    fmt_write(str, "[value: ");
    script_val_write(doc_val_data(doc, data->value.valId), str);
    fmt_write(str, "]");
    return;
  case ScriptExprKind_VarLoad:
    fmt_write(str, "[var-load: {}]", fmt_int(data->var_load.var));
    return;
  case ScriptExprKind_VarStore:
    fmt_write(str, "[var-store: {}]", fmt_int(data->var_store.var));
    script_expr_write_child(doc, data->var_store.val, indent + 1, str);
    return;
  case ScriptExprKind_MemLoad:
    fmt_write(str, "[mem-load: ${}]", fmt_int(data->mem_load.key));
    return;
  case ScriptExprKind_MemStore:
    fmt_write(str, "[mem-store: ${}]", fmt_int(data->mem_store.key));
    script_expr_write_child(doc, data->mem_store.val, indent + 1, str);
    return;
  case ScriptExprKind_Intrinsic: {
    fmt_write(str, "[intrinsic: {}]", script_intrinsic_fmt(data->intrinsic.intrinsic));
    const ScriptExpr* args     = expr_set_data(doc, data->block.exprSet);
    const u32         argCount = script_intrinsic_arg_count(data->intrinsic.intrinsic);
    for (u32 i = 0; i != argCount; ++i) {
      script_expr_write_child(doc, args[i], indent + 1, str);
    }
    return;
  }
  case ScriptExprKind_Block: {
    fmt_write(str, "[block]");
    const ScriptExpr* exprs = expr_set_data(doc, data->block.exprSet);
    for (u32 i = 0; i != data->block.exprCount; ++i) {
      script_expr_write_child(doc, exprs[i], indent + 1, str);
    }
    return;
  }
  case ScriptExprKind_Extern: {
    fmt_write(str, "[extern: {}]", fmt_int(data->extern_.func));
    const ScriptExpr* args = expr_set_data(doc, data->extern_.argSet);
    for (u16 i = 0; i != data->extern_.argCount; ++i) {
      script_expr_write_child(doc, args[i], indent + 1, str);
    }
    return;
  }
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

String script_expr_scratch(const ScriptDoc* doc, const ScriptExpr expr) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, usize_kibibyte * 8, 1);
  DynString str        = dynstring_create_over(scratchMem);

  const u32 indent = 0;
  script_expr_write(doc, expr, indent, &str);

  const String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}
