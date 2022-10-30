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

static ScriptValId script_register_val(ScriptDoc* doc, const ScriptVal val) {
  const ScriptValId id                      = (ScriptValId)doc->values.size;
  *dynarray_push_t(&doc->values, ScriptVal) = val;
  return id;
}

static ScriptExpr script_expr_add(ScriptDoc* doc, const ScriptExprData data) {
  const ScriptExpr expr                         = (ScriptExpr)doc->exprs.size;
  *dynarray_push_t(&doc->exprs, ScriptExprData) = data;
  return expr;
}

static ScriptExprData* script_expr_data(const ScriptDoc* doc, const ScriptExpr expr) {
  diag_assert_msg(expr < doc->exprs.size, "Out of bounds ScriptExpr");
  return dynarray_at_t(&doc->exprs, expr, ScriptExprData);
}

ScriptDoc* script_create(Allocator* alloc) {
  ScriptDoc* doc = alloc_alloc_t(alloc, ScriptDoc);
  *doc           = (ScriptDoc){
      .exprs  = dynarray_create_t(alloc, ScriptExpr, 64),
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
  const ScriptValId valId = script_register_val(doc, val);
  return script_expr_add(
      doc,
      (ScriptExprData){
          .type     = ScriptExprType_Lit,
          .data_lit = {.valId = valId},
      });
}

ScriptExprType script_expr_type(const ScriptDoc* doc, const ScriptExpr expr) {
  return script_expr_data(doc, expr)->type;
}
