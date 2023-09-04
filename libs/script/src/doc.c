#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"

#include "doc_internal.h"

static ScriptExpr script_doc_expr_add(ScriptDoc* doc, const ScriptExprData data) {
  const ScriptExpr expr                         = (ScriptExpr)doc->exprs.size;
  *dynarray_push_t(&doc->exprs, ScriptExprData) = data;
  return expr;
}

static ScriptExprData* script_doc_expr_data(const ScriptDoc* doc, const ScriptExpr expr) {
  diag_assert_msg(expr < doc->exprs.size, "Out of bounds ScriptExpr");
  return dynarray_at_t(&doc->exprs, expr, ScriptExprData);
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

ScriptDoc* script_create(Allocator* alloc) {
  ScriptDoc* doc = alloc_alloc_t(alloc, ScriptDoc);
  *doc           = (ScriptDoc){
      .exprs  = dynarray_create_t(alloc, ScriptExprData, 64),
      .values = dynarray_create_t(alloc, ScriptVal, 32),
      .alloc  = alloc,
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
  dynarray_destroy(&doc->exprs);
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

ScriptExpr script_add_op_nullary(ScriptDoc* doc, const ScriptOpNullary op) {
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type            = ScriptExprType_OpNullary,
          .data_op_nullary = {.op = op},
      });
}

ScriptExpr script_add_op_unary(ScriptDoc* doc, const ScriptExpr arg1, const ScriptOpUnary op) {
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type          = ScriptExprType_OpUnary,
          .data_op_unary = {.arg1 = arg1, .op = op},
      });
}

ScriptExpr script_add_op_binary(
    ScriptDoc* doc, const ScriptExpr arg1, const ScriptExpr arg2, const ScriptOpBinary op) {
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type           = ScriptExprType_OpBinary,
          .data_op_binary = {.arg1 = arg1, .arg2 = arg2, .op = op},
      });
}

ScriptExpr script_add_op_ternary(
    ScriptDoc*            doc,
    const ScriptExpr      arg1,
    const ScriptExpr      arg2,
    const ScriptExpr      arg3,
    const ScriptOpTernary op) {
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type            = ScriptExprType_OpTernary,
          .data_op_ternary = {.arg1 = arg1, .arg2 = arg2, .arg3 = arg3, .op = op},
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
  case ScriptExprType_OpNullary:
  case ScriptExprType_OpUnary:
  case ScriptExprType_OpBinary:
  case ScriptExprType_OpTernary:
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
  case ScriptExprType_OpNullary:
    return; // No children.
  case ScriptExprType_MemStore:
    script_expr_visit(doc, data->data_mem_store.val, ctx, visitor);
    return;
  case ScriptExprType_OpUnary:
    script_expr_visit(doc, data->data_op_unary.arg1, ctx, visitor);
    return;
  case ScriptExprType_OpBinary:
    script_expr_visit(doc, data->data_op_binary.arg1, ctx, visitor);
    script_expr_visit(doc, data->data_op_binary.arg2, ctx, visitor);
    return;
  case ScriptExprType_OpTernary:
    script_expr_visit(doc, data->data_op_ternary.arg1, ctx, visitor);
    script_expr_visit(doc, data->data_op_ternary.arg2, ctx, visitor);
    script_expr_visit(doc, data->data_op_ternary.arg3, ctx, visitor);
    return;
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
  case ScriptExprType_OpNullary:
    fmt_write(str, "[op-nullary: {}]", script_op_nullary_fmt(data->data_op_nullary.op));
    return;
  case ScriptExprType_OpUnary:
    fmt_write(str, "[op-unary: {}]", script_op_unary_fmt(data->data_op_unary.op));
    script_expr_str_write_child(doc, data->data_op_unary.arg1, indent + 1, str);
    return;
  case ScriptExprType_OpBinary:
    fmt_write(str, "[op-binary: {}]", script_op_binary_fmt(data->data_op_binary.op));
    script_expr_str_write_child(doc, data->data_op_binary.arg1, indent + 1, str);
    script_expr_str_write_child(doc, data->data_op_binary.arg2, indent + 1, str);
    return;
  case ScriptExprType_OpTernary:
    fmt_write(str, "[op-ternary: {}]", script_op_ternary_fmt(data->data_op_ternary.op));
    script_expr_str_write_child(doc, data->data_op_ternary.arg1, indent + 1, str);
    script_expr_str_write_child(doc, data->data_op_ternary.arg2, indent + 1, str);
    script_expr_str_write_child(doc, data->data_op_ternary.arg3, indent + 1, str);
    return;
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
