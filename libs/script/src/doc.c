#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"

#include "doc_internal.h"

static ScriptExpr script_doc_expr_add(ScriptDoc* doc, const ScriptExprData data) {
  const ScriptExpr expr                            = (ScriptExpr)doc->exprData.size;
  *dynarray_push_t(&doc->exprData, ScriptExprData) = data;
  return expr;
}

static ScriptExprData* script_doc_expr_data(const ScriptDoc* doc, const ScriptExpr expr) {
  diag_assert_msg(expr < doc->exprData.size, "Out of bounds ScriptExpr");
  return dynarray_at_t(&doc->exprData, expr, ScriptExprData);
}

static ScriptValId script_doc_val_add(ScriptDoc* doc, const ScriptVal val) {
  const ScriptValId id                      = (ScriptValId)doc->values.size;
  *dynarray_push_t(&doc->values, ScriptVal) = val;
  return id;
}

static ScriptVal script_doc_val_data(const ScriptDoc* doc, const ScriptValId id) {
  diag_assert_msg(id < doc->values.size, "Out of bounds ScriptValId");
  return *dynarray_at_t(&doc->values, id, ScriptVal);
}

static void script_doc_constant_add(ScriptDoc* doc, const String name, const ScriptVal val) {
  const StringHash  nameHash = string_hash(name);
  const ScriptValId valId    = script_doc_val_add(doc, val);

  array_for_t(doc->constants, ScriptConstant, constant) {
    if (!constant->nameHash) {
      *constant = (ScriptConstant){.nameHash = nameHash, .valId = valId};
      return;
    }
  }
  diag_crash_msg("Script constants count exceeded");
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
  *doc           = (ScriptDoc){
      .exprData = dynarray_create_t(alloc, ScriptExprData, 64),
      .exprSets = dynarray_create_t(alloc, ScriptExpr, 32),
      .values   = dynarray_create_t(alloc, ScriptVal, 32),
      .alloc    = alloc,
  };

  // Register build-in constants.
  script_doc_constant_add(doc, string_lit("null"), script_null());
  script_doc_constant_add(doc, string_lit("true"), script_bool(true));
  script_doc_constant_add(doc, string_lit("false"), script_bool(false));
  script_doc_constant_add(doc, string_lit("pi"), script_number(math_pi_f64));
  script_doc_constant_add(doc, string_lit("deg_to_rad"), script_number(math_deg_to_rad));
  script_doc_constant_add(doc, string_lit("rad_to_deg"), script_number(math_rad_to_deg));
  script_doc_constant_add(doc, string_lit("up"), script_vector3(geo_up));
  script_doc_constant_add(doc, string_lit("down"), script_vector3(geo_down));
  script_doc_constant_add(doc, string_lit("left"), script_vector3(geo_left));
  script_doc_constant_add(doc, string_lit("right"), script_vector3(geo_right));
  script_doc_constant_add(doc, string_lit("forward"), script_vector3(geo_forward));
  script_doc_constant_add(doc, string_lit("backward"), script_vector3(geo_backward));

  return doc;
}

void script_destroy(ScriptDoc* doc) {
  dynarray_destroy(&doc->exprData);
  dynarray_destroy(&doc->exprSets);
  dynarray_destroy(&doc->values);
  alloc_free_t(doc->alloc, doc);
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

ScriptExprType script_expr_type(const ScriptDoc* doc, const ScriptExpr expr) {
  return script_doc_expr_data(doc, expr)->type;
}

static void script_visitor_readonly(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  bool* isReadonly = ctx;
  switch (script_doc_expr_data(doc, expr)->type) {
  case ScriptExprType_MemStore:
    *isReadonly = false;
    return;
  case ScriptExprType_Value:
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
  case ScriptExprType_MemLoad:
    return; // No children.
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
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Unknown expression type");
  UNREACHABLE
}

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

ScriptExpr script_add_value_id(ScriptDoc* doc, const ScriptValId valId) {
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type       = ScriptExprType_Value,
          .data_value = {.valId = valId},
      });
}

ScriptValId script_doc_constant_lookup(const ScriptDoc* doc, const StringHash nameHash) {
  diag_assert_msg(nameHash, "Constant name cannot be empty");

  array_for_t(doc->constants, ScriptConstant, constant) {
    if (constant->nameHash == nameHash) {
      return constant->valId;
    }
  }
  return sentinel_u32;
}
