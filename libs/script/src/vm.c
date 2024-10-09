#include "core_diag.h"
#include "script_binder.h"
#include "script_vm.h"

#include "doc_internal.h"
#include "val_internal.h"

typedef struct {
  const ScriptDoc*    doc;
  ScriptMem*          m;
  const ScriptBinder* binder;
  void*               bindCtx;
  ScriptPanic         panic;
  ScriptVal           regs[script_vm_regs];
} ScriptVmContext;

static ScriptVal vm_run(ScriptVmContext* ctx, const String code) {
  u8*       ip    = mem_begin(code);
  const u8* ipEnd = mem_end(code);
  for (;;) {
    if (UNLIKELY(ip == ipEnd)) {
      ctx->panic = (ScriptPanic){.kind = ScriptPanic_CorruptCode};
      return script_null();
    }
    const ScriptOp op = *ip++;
    switch (op) {
    case ScriptOp_Fail:
      ctx->panic = (ScriptPanic){.kind = ScriptPanic_ExecutionFailed};
      return script_null();
    case ScriptOp_Return:
      return ctx->regs[0];
    default:
      ctx->panic = (ScriptPanic){.kind = ScriptPanic_CorruptCode};
      return script_null();
    }
  }
}

ScriptVmResult script_vm_eval(
    const ScriptDoc*    doc,
    const String        code,
    ScriptMem*          m,
    const ScriptBinder* binder,
    void*               bindCtx) {
  if (binder) {
    diag_assert_msg(script_binder_hash(binder) == doc->binderHash, "Incompatible binder");
  }
  ScriptVmContext ctx = {
      .doc     = doc,
      .m       = m,
      .binder  = binder,
      .bindCtx = bindCtx,
  };

  ScriptVmResult res;
  res.val   = vm_run(&ctx, code);
  res.panic = ctx.panic;

  return res;
}
