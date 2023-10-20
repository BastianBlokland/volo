#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_eval.h"

#include "doc_internal.h"

static ScriptExpr script_doc_expr_add(ScriptDoc* doc, const ScriptExprData data) {
  const ScriptExpr expr                            = (ScriptExpr)doc->exprData.size;
  *dynarray_push_t(&doc->exprData, ScriptExprData) = data;
  return expr;
}

INLINE_HINT static ScriptExprData* script_doc_expr_data(const ScriptDoc* doc, const ScriptExpr e) {
  diag_assert_msg(e < doc->exprData.size, "Out of bounds ScriptExpr");
  return dynarray_begin_t(&doc->exprData, ScriptExprData) + e;
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

static const ScriptExpr* script_doc_expr_set_data(const ScriptDoc* doc, const ScriptExprSet set) {
  return dynarray_begin_t(&doc->exprSets, ScriptExpr) + set;
}

ScriptDoc* script_create(Allocator* alloc) {
  ScriptDoc* doc = alloc_alloc_t(alloc, ScriptDoc);

  *doc = (ScriptDoc){
      .exprData = dynarray_create_t(alloc, ScriptExprData, 64),
      .exprSets = dynarray_create_t(alloc, ScriptExpr, 32),
      .values   = dynarray_create_t(alloc, ScriptVal, 32),
      .alloc    = alloc,
  };
  return doc;
}

void script_destroy(ScriptDoc* doc) {
  dynarray_destroy(&doc->exprData);
  dynarray_destroy(&doc->exprSets);
  dynarray_destroy(&doc->values);
  alloc_free_t(doc->alloc, doc);
}

void script_clear(ScriptDoc* doc) {
  dynarray_clear(&doc->exprData);
  dynarray_clear(&doc->exprSets);
  dynarray_clear(&doc->values);
}

ScriptExpr script_add_value(ScriptDoc* doc, const ScriptVal val) {
  const ScriptValId valId = script_doc_val_add(doc, val);
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type       = ScriptExprType_Value,
          .data_value = {.valId = valId},
      });
}

ScriptExpr script_add_var_load(ScriptDoc* doc, const ScriptVarId var) {
  diag_assert_msg(var < script_var_count, "Out of bounds script variable");
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type          = ScriptExprType_VarLoad,
          .data_var_load = {.var = var},
      });
}

ScriptExpr script_add_var_store(ScriptDoc* doc, const ScriptVarId var, const ScriptExpr val) {
  diag_assert_msg(var < script_var_count, "Out of bounds script variable");
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type           = ScriptExprType_VarStore,
          .data_var_store = {.var = var, .val = val},
      });
}

ScriptExpr script_add_mem_load(ScriptDoc* doc, const StringHash key) {
  diag_assert_msg(key, "Empty key is not valid");
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type          = ScriptExprType_MemLoad,
          .data_mem_load = {.key = key},
      });
}

ScriptExpr script_add_mem_store(ScriptDoc* doc, const StringHash key, const ScriptExpr val) {
  diag_assert_msg(key, "Empty key is not valid");
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type           = ScriptExprType_MemStore,
          .data_mem_store = {.key = key, .val = val},
      });
}

ScriptExpr script_add_intrinsic(ScriptDoc* doc, const ScriptIntrinsic i, const ScriptExpr args[]) {
  const u32           argCount = script_intrinsic_arg_count(i);
  const ScriptExprSet argSet   = script_doc_expr_set_add(doc, args, argCount);
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type           = ScriptExprType_Intrinsic,
          .data_intrinsic = {.argSet = argSet, .intrinsic = i},
      });
}

ScriptExpr script_add_block(ScriptDoc* doc, const ScriptExpr exprs[], const u32 exprCount) {
  diag_assert_msg(exprCount, "Zero sized blocks are not supported");

  const ScriptExprSet set = script_doc_expr_set_add(doc, exprs, exprCount);
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type       = ScriptExprType_Block,
          .data_block = {.exprSet = set, .exprCount = exprCount},
      });
}

ScriptExpr script_add_extern(
    ScriptDoc* doc, const ScriptBinderSlot func, const ScriptExpr args[], const u32 argCount) {

  const ScriptExprSet argSet = script_doc_expr_set_add(doc, args, argCount);
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type        = ScriptExprType_Extern,
          .data_extern = {.func = func, .argSet = argSet, .argCount = argCount},
      });
}

ScriptExprType script_expr_type(const ScriptDoc* doc, const ScriptExpr expr) {
  return script_doc_expr_data(doc, expr)->type;
}

static void script_visitor_readonly(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  bool* isReadonly = ctx;
  switch (script_doc_expr_data(doc, expr)->type) {
  case ScriptExprType_MemStore:
  case ScriptExprType_Extern:
    *isReadonly = false;
    return;
  case ScriptExprType_Value:
  case ScriptExprType_VarLoad:
  case ScriptExprType_VarStore: // NOTE: Variables are volatile so are considered readonly.
  case ScriptExprType_MemLoad:
  case ScriptExprType_Intrinsic:
  case ScriptExprType_Block:
    return;
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Unknown expression type");
  UNREACHABLE
}

bool script_expr_readonly(const ScriptDoc* doc, const ScriptExpr expr) {
  bool isReadonly = true;
  script_expr_visit(doc, expr, &isReadonly, script_visitor_readonly);
  return isReadonly;
}

static void script_visitor_static(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  bool*                 isStatic = ctx;
  const ScriptExprData* data     = script_doc_expr_data(doc, expr);
  switch (data->type) {
  case ScriptExprType_MemLoad:
  case ScriptExprType_MemStore:
  case ScriptExprType_VarLoad:
  case ScriptExprType_VarStore:
  case ScriptExprType_Extern:
    *isStatic = false;
    return;
  case ScriptExprType_Intrinsic: {
    if (!script_intrinsic_deterministic(data->data_intrinsic.intrinsic)) {
      *isStatic = false;
    }
    return;
  }
  case ScriptExprType_Value:
  case ScriptExprType_Block:
    return;
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Unknown expression type");
  UNREACHABLE
}

bool script_expr_static(const ScriptDoc* doc, const ScriptExpr expr) {
  bool isStatic = true;
  script_expr_visit(doc, expr, &isStatic, script_visitor_static);
  return isStatic;
}

bool script_expr_always_truthy(const ScriptDoc* doc, const ScriptExpr expr) {
  if (!script_expr_static(doc, expr)) {
    return false;
  }
  const ScriptEvalResult evalRes = script_eval(doc, null, expr, null, null);
  return evalRes.error == ScriptErrorRuntime_None && script_truthy(evalRes.val);
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
  const ScriptExprData* data = script_doc_expr_data(doc, expr);
  switch (data->type) {
  case ScriptExprType_Value:
  case ScriptExprType_VarLoad:
  case ScriptExprType_MemLoad:
    return; // No children.
  case ScriptExprType_VarStore:
    script_expr_visit(doc, data->data_var_store.val, ctx, visitor);
    return;
  case ScriptExprType_MemStore:
    script_expr_visit(doc, data->data_mem_store.val, ctx, visitor);
    return;
  case ScriptExprType_Intrinsic: {
    const ScriptExpr* args     = script_doc_expr_set_data(doc, data->data_intrinsic.argSet);
    const u32         argCount = script_intrinsic_arg_count(data->data_intrinsic.intrinsic);
    for (u32 i = 0; i != argCount; ++i) {
      script_expr_visit(doc, args[i], ctx, visitor);
    }
    return;
  }
  case ScriptExprType_Block: {
    const ScriptExpr* exprs = script_doc_expr_set_data(doc, data->data_block.exprSet);
    for (u32 i = 0; i != data->data_block.exprCount; ++i) {
      script_expr_visit(doc, exprs[i], ctx, visitor);
    }
    return;
  }
  case ScriptExprType_Extern: {
    const ScriptExpr* args = script_doc_expr_set_data(doc, data->data_extern.argSet);
    for (u32 i = 0; i != data->data_extern.argCount; ++i) {
      script_expr_visit(doc, args[i], ctx, visitor);
    }
    return;
  }
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Unknown expression type");
  UNREACHABLE
}

ScriptDocSignal script_expr_always_uncaught_signal(const ScriptDoc* doc, const ScriptExpr expr) {
  const ScriptExprData* data = script_doc_expr_data(doc, expr);
  switch (data->type) {
  case ScriptExprType_Value:
  case ScriptExprType_VarLoad:
  case ScriptExprType_MemLoad:
    return ScriptDocSignal_None; // No children.
  case ScriptExprType_VarStore:
    return script_expr_always_uncaught_signal(doc, data->data_var_store.val);
  case ScriptExprType_MemStore:
    return script_expr_always_uncaught_signal(doc, data->data_mem_store.val);
  case ScriptExprType_Intrinsic: {
    const ScriptExpr* args = script_doc_expr_set_data(doc, data->data_intrinsic.argSet);
    const u32 argCount = script_intrinsic_arg_count_always_reached(data->data_intrinsic.intrinsic);
    switch (data->data_intrinsic.intrinsic) {
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
        const ScriptEvalResult res = script_eval(doc, null, args[0], null, null);
        if (res.error == ScriptErrorRuntime_None) {
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
  case ScriptExprType_Block: {
    const ScriptExpr* exprs = script_doc_expr_set_data(doc, data->data_block.exprSet);
    for (u32 i = 0; i != data->data_block.exprCount; ++i) {
      const ScriptDocSignal sig = script_expr_always_uncaught_signal(doc, exprs[i]);
      if (sig) {
        return sig;
      }
    }
    return ScriptDocSignal_None;
  }
  case ScriptExprType_Extern: {
    const ScriptExpr* args = script_doc_expr_set_data(doc, data->data_extern.argSet);
    for (u32 i = 0; i != data->data_extern.argCount; ++i) {
      const ScriptDocSignal sig = script_expr_always_uncaught_signal(doc, args[i]);
      if (sig) {
        return sig;
      }
    }
    return ScriptDocSignal_None;
  }
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Unknown expression type");
  UNREACHABLE
}

u32 script_values_total(const ScriptDoc* doc) { return (u32)doc->values.size; }

static void script_expr_str_write_sep(const u32 indent, DynString* str) {
  dynstring_append_char(str, '\n');
  dynstring_append_chars(str, ' ', indent * 2);
}

static void script_expr_str_write_child(
    const ScriptDoc* doc, const ScriptExpr expr, const u32 indent, DynString* str) {
  script_expr_str_write_sep(indent, str);
  script_expr_str_write(doc, expr, indent, str);
}

void script_expr_str_write(
    const ScriptDoc* doc, const ScriptExpr expr, const u32 indent, DynString* str) {
  const ScriptExprData* data = script_doc_expr_data(doc, expr);
  switch (data->type) {
  case ScriptExprType_Value:
    fmt_write(str, "[value: ");
    script_val_str_write(script_doc_val_data(doc, data->data_value.valId), str);
    fmt_write(str, "]");
    return;
  case ScriptExprType_VarLoad:
    fmt_write(str, "[var-load: {}]", fmt_int(data->data_var_load.var));
    return;
  case ScriptExprType_VarStore:
    fmt_write(str, "[var-store: {}]", fmt_int(data->data_var_store.var));
    script_expr_str_write_child(doc, data->data_var_store.val, indent + 1, str);
    return;
  case ScriptExprType_MemLoad:
    fmt_write(str, "[mem-load: ${}]", fmt_int(data->data_mem_load.key));
    return;
  case ScriptExprType_MemStore:
    fmt_write(str, "[mem-store: ${}]", fmt_int(data->data_mem_store.key));
    script_expr_str_write_child(doc, data->data_mem_store.val, indent + 1, str);
    return;
  case ScriptExprType_Intrinsic: {
    fmt_write(str, "[intrinsic: {}]", script_intrinsic_fmt(data->data_intrinsic.intrinsic));
    const ScriptExpr* args     = script_doc_expr_set_data(doc, data->data_block.exprSet);
    const u32         argCount = script_intrinsic_arg_count(data->data_intrinsic.intrinsic);
    for (u32 i = 0; i != argCount; ++i) {
      script_expr_str_write_child(doc, args[i], indent + 1, str);
    }
    return;
  }
  case ScriptExprType_Block: {
    fmt_write(str, "[block]");
    const ScriptExpr* exprs = script_doc_expr_set_data(doc, data->data_block.exprSet);
    for (u32 i = 0; i != data->data_block.exprCount; ++i) {
      script_expr_str_write_child(doc, exprs[i], indent + 1, str);
    }
    return;
  }
  case ScriptExprType_Extern: {
    fmt_write(str, "[extern: {}]", fmt_int(data->data_extern.func));
    const ScriptExpr* args = script_doc_expr_set_data(doc, data->data_extern.argSet);
    for (u32 i = 0; i != data->data_extern.argCount; ++i) {
      script_expr_str_write_child(doc, args[i], indent + 1, str);
    }
    return;
  }
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Unknown expression type");
  UNREACHABLE
}

String script_expr_str_scratch(const ScriptDoc* doc, const ScriptExpr expr) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, usize_kibibyte * 8, 1);
  DynString str        = dynstring_create_over(scratchMem);

  const u32 indent = 0;
  script_expr_str_write(doc, expr, indent, &str);

  const String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}
