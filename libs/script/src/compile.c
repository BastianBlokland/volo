#include "core_diag.h"
#include "script_compile.h"

#include "doc_internal.h"

typedef struct {
  const ScriptDoc* doc;
  DynString*       out;
} ScriptCompileContext;

static ScriptCompileResult compile_expr(ScriptCompileContext* ctx, const ScriptExpr expr) {
  (void)ctx;
  (void)expr;
  return ScriptCompileResult_Success;
}

ScriptCompileResult script_compile(const ScriptDoc* doc, const ScriptExpr expr, DynString* out) {
  ScriptCompileContext ctx = {
      .doc = doc,
      .out = out,
  };
  return compile_expr(&ctx, expr);
}
