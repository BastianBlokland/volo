#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_stringtable.h"
#include "script_eval.h"
#include "script_pos.h"

#include "doc_internal.h"

static ScriptExpr script_doc_expr_add(
    ScriptDoc* doc, const ScriptRange range, const ScriptExprKind kind, const ScriptExprData data) {
  const ScriptExpr expr                            = (ScriptExpr)doc->exprData.size;
  *dynarray_push_t(&doc->exprData, ScriptExprData) = data;
  *dynarray_push_t(&doc->exprKinds, u8)            = (u8)kind;
  *dynarray_push_t(&doc->exprRanges, ScriptRange)  = range;
  return expr;
}

static ScriptValId script_doc_val_add(ScriptDoc* doc, const ScriptVal val) {
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

static ScriptVal script_doc_val_data(const ScriptDoc* doc, const ScriptValId id) {
  diag_assert_msg(id < doc->values.size, "Out of bounds ScriptValId");
  return dynarray_begin_t(&doc->values, ScriptVal)[id];
}

static ScriptExprSet
script_doc_expr_set_add(ScriptDoc* doc, const ScriptExpr exprs[], const u32 count) {
  const ScriptExprSet set = (ScriptExprSet)doc->exprSets.size;
  mem_cpy(dynarray_push(&doc->exprSets, count), mem_create(exprs, sizeof(ScriptExpr) * count));
  return set;
}

static void script_validate_subrange(
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

static void script_validate_subrange_set(
    MAYBE_UNUSED const ScriptDoc*    doc,
    MAYBE_UNUSED const ScriptRange   range,
    MAYBE_UNUSED const ScriptExprSet set,
    MAYBE_UNUSED const u32           count) {
#ifndef VOLO_FAST
  diag_assert_msg(!count || set < doc->exprSets.size, "Out of bounds ScriptExprSet");
  const ScriptExpr* exprs = expr_set_data(doc, set);
  for (u32 i = 0; i != count; ++i) {
    script_validate_subrange(doc, range, exprs[i]);
  }
#endif
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

ScriptExpr script_add_value(ScriptDoc* doc, const ScriptRange range, const ScriptVal val) {
  const ScriptValId valId = script_doc_val_add(doc, val);
  return script_doc_expr_add(
      doc, range, ScriptExprKind_Value, (ScriptExprData){.value = {.valId = valId}});
}

ScriptExpr script_add_var_load(ScriptDoc* doc, const ScriptRange range, const ScriptVarId var) {
  diag_assert_msg(var < script_var_count, "Out of bounds script variable");
  return script_doc_expr_add(
      doc, range, ScriptExprKind_VarLoad, (ScriptExprData){.var_load = {.var = var}});
}

ScriptExpr script_add_var_store(
    ScriptDoc* doc, const ScriptRange range, const ScriptVarId var, const ScriptExpr val) {
  diag_assert_msg(var < script_var_count, "Out of bounds script variable");
  script_validate_subrange(doc, range, val);
  return script_doc_expr_add(
      doc, range, ScriptExprKind_VarStore, (ScriptExprData){.var_store = {.var = var, .val = val}});
}

ScriptExpr script_add_mem_load(ScriptDoc* doc, const ScriptRange range, const StringHash key) {
  diag_assert_msg(key, "Empty key is not valid");
  return script_doc_expr_add(
      doc, range, ScriptExprKind_MemLoad, (ScriptExprData){.mem_load = {.key = key}});
}

ScriptExpr script_add_mem_store(
    ScriptDoc* doc, const ScriptRange range, const StringHash key, const ScriptExpr val) {
  diag_assert_msg(key, "Empty key is not valid");
  script_validate_subrange(doc, range, val);
  return script_doc_expr_add(
      doc, range, ScriptExprKind_MemStore, (ScriptExprData){.mem_store = {.key = key, .val = val}});
}

ScriptExpr script_add_intrinsic(
    ScriptDoc* doc, const ScriptRange range, const ScriptIntrinsic i, const ScriptExpr args[]) {
  const u32           argCount = script_intrinsic_arg_count(i);
  const ScriptExprSet argSet   = script_doc_expr_set_add(doc, args, argCount);
  script_validate_subrange_set(doc, range, argSet, argCount);
  return script_doc_expr_add(
      doc,
      range,
      ScriptExprKind_Intrinsic,
      (ScriptExprData){.intrinsic = {.argSet = argSet, .intrinsic = i}});
}

ScriptExpr script_add_block(
    ScriptDoc* doc, const ScriptRange range, const ScriptExpr exprs[], const u32 exprCount) {
  diag_assert_msg(exprCount, "Zero sized blocks are not supported");

  const ScriptExprSet set = script_doc_expr_set_add(doc, exprs, exprCount);
  script_validate_subrange_set(doc, range, set, exprCount);
  return script_doc_expr_add(
      doc,
      range,
      ScriptExprKind_Block,
      (ScriptExprData){.block = {.exprSet = set, .exprCount = exprCount}});
}

ScriptExpr script_add_extern(
    ScriptDoc*             doc,
    const ScriptRange      range,
    const ScriptBinderSlot func,
    const ScriptExpr       args[],
    const u16              argCount) {
  const ScriptExprSet argSet = script_doc_expr_set_add(doc, args, argCount);
  script_validate_subrange_set(doc, range, argSet, argCount);
  return script_doc_expr_add(
      doc,
      range,
      ScriptExprKind_Extern,
      (ScriptExprData){.extern_ = {.func = func, .argSet = argSet, .argCount = argCount}});
}

ScriptExpr script_add_anon_value(ScriptDoc* doc, const ScriptVal val) {
  return script_add_value(doc, script_range_sentinel, val);
}

ScriptExpr script_add_anon_var_load(ScriptDoc* doc, const ScriptVarId var) {
  return script_add_var_load(doc, script_range_sentinel, var);
}

ScriptExpr script_add_anon_var_store(ScriptDoc* doc, const ScriptVarId var, const ScriptExpr val) {
  return script_add_var_store(doc, script_range_sentinel, var, val);
}

ScriptExpr script_add_anon_mem_load(ScriptDoc* doc, const StringHash key) {
  return script_add_mem_load(doc, script_range_sentinel, key);
}

ScriptExpr script_add_anon_mem_store(ScriptDoc* doc, const StringHash key, const ScriptExpr val) {
  return script_add_mem_store(doc, script_range_sentinel, key, val);
}

ScriptExpr
script_add_anon_intrinsic(ScriptDoc* doc, const ScriptIntrinsic i, const ScriptExpr args[]) {
  return script_add_intrinsic(doc, script_range_sentinel, i, args);
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

static void script_visitor_readonly(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  bool* isReadonly = ctx;
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_MemStore:
  case ScriptExprKind_Extern:
    *isReadonly = false;
    return;
  case ScriptExprKind_Value:
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_VarStore: // NOTE: Variables are volatile so are considered readonly.
  case ScriptExprKind_MemLoad:
  case ScriptExprKind_Intrinsic:
  case ScriptExprKind_Block:
    return;
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

bool script_expr_readonly(const ScriptDoc* doc, const ScriptExpr expr) {
  diag_assert_msg(expr < doc->exprData.size, "Out of bounds ScriptExpr");

  bool isReadonly = true;
  script_expr_visit(doc, expr, &isReadonly, script_visitor_readonly);
  return isReadonly;
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
  const ScriptExprData* data  = expr_data(doc, expr);
  const ScriptRange     range = script_expr_range(doc, expr);
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_Value:
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_MemLoad:
    return expr; // No children.
  case ScriptExprKind_VarStore: {
    const ScriptExpr newVal = script_expr_rewrite(doc, data->var_store.val, ctx, rewriter);
    if (newVal == data->var_store.val) {
      return expr; // Not rewritten.
    }
    return script_add_var_store(doc, range, data->var_store.var, newVal);
  }
  case ScriptExprKind_MemStore: {
    const ScriptExpr newVal = script_expr_rewrite(doc, data->mem_store.val, ctx, rewriter);
    if (newVal == data->mem_store.val) {
      return expr; // Not rewritten.
    }
    return script_add_mem_store(doc, range, data->mem_store.val, newVal);
  }
  case ScriptExprKind_Intrinsic: {
    const u32         argCount     = script_intrinsic_arg_count(data->intrinsic.intrinsic);
    const ScriptExpr* args         = expr_set_data(doc, data->intrinsic.argSet);
    ScriptExpr*       newArgs      = mem_stack(sizeof(ScriptExpr) * argCount).ptr;
    bool              anyRewritten = false;
    for (u32 i = 0; i != argCount; ++i) {
      newArgs[i] = script_expr_rewrite(doc, args[i], ctx, rewriter);
      anyRewritten |= newArgs[i] != args[i];
    }
    if (!anyRewritten) {
      return expr; // Not rewritten.
    }
    return script_add_intrinsic(doc, range, data->intrinsic.intrinsic, newArgs);
  }
  case ScriptExprKind_Block: {
    const ScriptExpr* exprs        = expr_set_data(doc, data->block.exprSet);
    ScriptExpr*       newExprs     = mem_stack(sizeof(ScriptExpr) * data->block.exprCount).ptr;
    bool              anyRewritten = false;
    for (u32 i = 0; i != data->block.exprCount; ++i) {
      newExprs[i] = script_expr_rewrite(doc, exprs[i], ctx, rewriter);
      anyRewritten |= newExprs[i] != exprs[i];
    }
    if (!anyRewritten) {
      return expr; // Not rewritten.
    }
    return script_add_block(doc, range, newExprs, data->block.exprCount);
  }
  case ScriptExprKind_Extern: {
    const ScriptExpr* args         = expr_set_data(doc, data->extern_.argSet);
    ScriptExpr*       newArgs      = mem_stack(sizeof(ScriptExpr) * data->extern_.argCount).ptr;
    bool              anyRewritten = false;
    for (u32 i = 0; i != data->extern_.argCount; ++i) {
      newArgs[i] = script_expr_rewrite(doc, args[i], ctx, rewriter);
      anyRewritten |= newArgs[i] != args[i];
    }
    if (!anyRewritten) {
      return expr; // Not rewritten.
    }
    return script_add_extern(doc, range, data->extern_.func, newArgs, data->extern_.argCount);
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
    fmt_write(str, "[value: '");
    script_val_write(script_doc_val_data(doc, data->value.valId), str);
    fmt_write(str, "']");
    return;
  case ScriptExprKind_VarLoad:
    fmt_write(str, "[var-load: {}]", fmt_int(data->var_load.var));
    return;
  case ScriptExprKind_VarStore:
    fmt_write(str, "[var-store: {}]", fmt_int(data->var_store.var));
    script_expr_write_child(doc, data->var_store.val, indent + 1, str);
    return;
  case ScriptExprKind_MemLoad: {
    const String keyName = stringtable_lookup(g_stringtable, data->mem_load.key);
    fmt_write(str, "[mem-load: ${}", fmt_int(data->mem_load.key));
    if (!string_is_empty(keyName)) {
      fmt_write(str, " '{}'", fmt_text(keyName));
    }
    dynstring_append_char(str, ']');
    return;
  }
  case ScriptExprKind_MemStore: {
    const String keyName = stringtable_lookup(g_stringtable, data->mem_store.key);
    fmt_write(str, "[mem-store: ${}", fmt_int(data->mem_store.key));
    script_expr_write_child(doc, data->mem_store.val, indent + 1, str);
    if (!string_is_empty(keyName)) {
      fmt_write(str, " '{}'", fmt_text(keyName));
    }
    dynstring_append_char(str, ']');
    return;
  }
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
