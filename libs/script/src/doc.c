#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_doc.h"

typedef u32 ScriptValId;

typedef struct {
  ScriptValId valId;
} ScriptExprValue;

typedef struct {
  StringHash key;
} ScriptExprLoad;

typedef struct {
  ScriptExpr  lhs;
  ScriptExpr  rhs;
  ScriptOpBin op;
} ScriptExprOpBin;

typedef struct {
  ScriptExprType type;
  union {
    ScriptExprValue data_value;
    ScriptExprLoad  data_load;
    ScriptExprOpBin data_op_bin;
  };
} ScriptExprData;

struct sScriptDoc {
  DynArray   exprs;  // ScriptExprData[]
  DynArray   values; // ScriptVal[]
  Allocator* alloc;
};

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

ScriptDoc* script_create(Allocator* alloc) {
  ScriptDoc* doc = alloc_alloc_t(alloc, ScriptDoc);
  *doc           = (ScriptDoc){
      .exprs  = dynarray_create_t(alloc, ScriptExprData, 64),
      .values = dynarray_create_t(alloc, ScriptVal, 32),
      .alloc  = alloc,
  };
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

ScriptExpr script_add_load(ScriptDoc* doc, const StringHash key) {
  diag_assert_msg(key, "Empty key is not valid");
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type      = ScriptExprType_Load,
          .data_load = {.key = key},
      });
}

ScriptExpr script_add_op_bin(
    ScriptDoc* doc, const ScriptExpr lhs, const ScriptExpr rhs, const ScriptOpBin op) {
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type        = ScriptExprType_OpBin,
          .data_op_bin = {.lhs = lhs, .rhs = rhs, .op = op},
      });
}

ScriptExprType script_expr_type(const ScriptDoc* doc, const ScriptExpr expr) {
  return script_doc_expr_data(doc, expr)->type;
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
  case ScriptExprType_Load:
    fmt_write(str, "[load: ${}]", fmt_int(data->data_load.key));
    return;
  case ScriptExprType_OpBin:
    fmt_write(str, "[op-bin: {}]", script_op_bin_fmt(data->data_op_bin.op));
    script_expr_str_write_child(doc, data->data_op_bin.lhs, indent + 1, str);
    script_expr_str_write_child(doc, data->data_op_bin.rhs, indent + 1, str);
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
