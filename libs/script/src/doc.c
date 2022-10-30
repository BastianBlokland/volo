#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_doc.h"

typedef struct {
  u32 dummy;
} ScriptExpr;

struct sScriptDoc {
  DynArray   exprs; // ScriptExpr[]
  Allocator* alloc;
};

ScriptDoc* script_create(Allocator* alloc, usize exprCapacity) {
  ScriptDoc* doc = alloc_alloc_t(alloc, ScriptDoc);
  *doc           = (ScriptDoc){
      .exprs = dynarray_create_t(alloc, ScriptExpr, exprCapacity),
      .alloc = alloc,
  };
  return doc;
}

void script_destroy(ScriptDoc* doc) {
  dynarray_destroy(&doc->exprs);
  alloc_free_t(doc->alloc, doc);
}
