#include "core_diag.h"
#include "core_dynstring.h"
#include "script_compile.h"
#include "script_vm.h"

#include "doc_internal.h"

typedef enum {
  ScriptReg_Accum = 0,
} ScriptReg;

typedef struct {
  const ScriptDoc* doc;
  DynString*       out;
} ScriptCompileContext;

static void emit_fail(ScriptCompileContext* ctx) { dynstring_append_char(ctx->out, ScriptOp_Fail); }

static void emit_return(ScriptCompileContext* ctx) {
  dynstring_append_char(ctx->out, ScriptOp_Return);
}

static void emit_value(ScriptCompileContext* ctx, const u8 valId, const u8 regId) {
  diag_assert(regId < script_vm_regs);
  dynstring_append_char(ctx->out, ScriptOp_Value);
  dynstring_append_char(ctx->out, valId);
  dynstring_append_char(ctx->out, regId);
}

static ScriptCompileResult compile_value(ScriptCompileContext* ctx, const ScriptExpr e) {
  const ScriptExprValue* data = &expr_data(ctx->doc, e)->value;
  emit_value(ctx, data->valId, ScriptReg_Accum);
  return ScriptCompileResult_Success;
}

static ScriptCompileResult compile_expr(ScriptCompileContext* ctx, const ScriptExpr e) {
  switch (expr_kind(ctx->doc, e)) {
  case ScriptExprKind_Value:
    return compile_value(ctx, e);
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_VarStore:
  case ScriptExprKind_MemLoad:
  case ScriptExprKind_MemStore:
  case ScriptExprKind_Intrinsic:
  case ScriptExprKind_Block:
  case ScriptExprKind_Extern:
    emit_fail(ctx);
    return ScriptCompileResult_Success;
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

ScriptCompileResult script_compile(const ScriptDoc* doc, const ScriptExpr expr, DynString* out) {
  ScriptCompileContext ctx = {
      .doc = doc,
      .out = out,
  };
  const ScriptCompileResult res = compile_expr(&ctx, expr);
  if (res == ScriptCompileResult_Success) {
    emit_return(&ctx);
  }
  return res;
}
