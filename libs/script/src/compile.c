#include "core_diag.h"
#include "core_dynstring.h"
#include "script_compile.h"
#include "script_vm.h"

#include "doc_internal.h"

typedef struct {
  const ScriptDoc* doc;
  DynString*       out;
} ScriptCompileContext;

static void emit_return(ScriptCompileContext* ctx) {
  dynstring_append_char(ctx->out, ScriptOp_Return);
}

static ScriptCompileResult compile_expr(ScriptCompileContext* ctx, const ScriptExpr expr) {
  (void)expr;
  emit_return(ctx);
  return ScriptCompileResult_Success;
}

ScriptCompileResult script_compile(const ScriptDoc* doc, const ScriptExpr expr, DynString* out) {
  ScriptCompileContext ctx = {
      .doc = doc,
      .out = out,
  };
  return compile_expr(&ctx, expr);
}
