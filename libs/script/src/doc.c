#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_doc.h"

typedef u32 ScriptValId;

typedef struct {
  ScriptValId valId;
} ScriptExprLit;

typedef struct {
  ScriptExprType type;
  union {
    ScriptExprLit data_lit;
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

ScriptExpr script_add_lit(ScriptDoc* doc, const ScriptVal val) {
  const ScriptValId valId = script_doc_val_add(doc, val);
  return script_doc_expr_add(
      doc,
      (ScriptExprData){
          .type     = ScriptExprType_Lit,
          .data_lit = {.valId = valId},
      });
}

ScriptExprType script_expr_type(const ScriptDoc* doc, const ScriptExpr expr) {
  return script_doc_expr_data(doc, expr)->type;
}

void script_expr_str_write(const ScriptDoc* doc, const ScriptExpr expr, DynString* str) {
  const ScriptExprData* data = script_doc_expr_data(doc, expr);
  switch (data->type) {
  case ScriptExprType_Lit:
    fmt_write(str, "[lit: ");
    script_val_str_write(script_doc_val_data(doc, data->data_lit.valId), str);
    fmt_write(str, "]");
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

  script_expr_str_write(doc, expr, &str);

  const String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}
