#include "core_alloc.h"
#include "core_array.h"
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

INLINE_HINT static bool vm_reg_valid(ScriptVmContext* ctx, const u8 regId) {
  return regId < array_elems(ctx->regs);
}

INLINE_HINT static bool vm_val_valid(ScriptVmContext* ctx, const u8 valId) {
  return valId < ctx->doc->values.size;
}

static ScriptVal vm_run(ScriptVmContext* ctx, const String code) {
  const u8* ip    = mem_begin(code);
  const u8* ipEnd = mem_end(code);
  if (UNLIKELY(ip == ipEnd)) {
    goto Corrupt;
  }
  for (;;) {
    // clang-format off
    switch ((ScriptOp)*ip) {
    case ScriptOp_Fail: goto ExecFailed;
    case ScriptOp_Return: {
      if (UNLIKELY((ip += 2) > ipEnd)) goto Corrupt;
      const u8 regId = ip[-1];
      if(UNLIKELY(!vm_reg_valid(ctx, regId))) goto Corrupt;
      return ctx->regs[regId];
    }
    case ScriptOp_Move: {
      if (UNLIKELY((ip += 3) >= ipEnd)) goto Corrupt;
      const u8 regIdDst = ip[-2];
      const u8 regIdSrc = ip[-1];
      if(UNLIKELY(!vm_reg_valid(ctx, regIdDst))) goto Corrupt;
      if(UNLIKELY(!vm_reg_valid(ctx, regIdSrc))) goto Corrupt;
      ctx->regs[regIdDst] = ctx->regs[regIdSrc];
      continue;
    }
    case ScriptOp_Value: {
      if (UNLIKELY((ip += 3) >= ipEnd)) goto Corrupt;
      const u8 regId = ip[-2];
      const u8 valId = ip[-1];
      if(UNLIKELY(!vm_reg_valid(ctx, regId))) goto Corrupt;
      if(UNLIKELY(!vm_val_valid(ctx, valId))) goto Corrupt;
      ctx->regs[regId] = dynarray_begin_t(&ctx->doc->values, ScriptVal)[valId];
      continue;
    }
    case ScriptOp_Add: {
      if (UNLIKELY((ip += 3) >= ipEnd)) goto Corrupt;
      const u8 regIdDst = ip[-2];
      const u8 regIdSrc = ip[-1];
      if(UNLIKELY(!vm_reg_valid(ctx, regIdDst))) goto Corrupt;
      if(UNLIKELY(!vm_reg_valid(ctx, regIdSrc))) goto Corrupt;
      ctx->regs[regIdDst] = script_val_add(ctx->regs[regIdDst], ctx->regs[regIdSrc]);
      continue;
    }
    }
    // clang-format on
    goto Corrupt;
  }

Corrupt:
  ctx->panic = (ScriptPanic){.kind = ScriptPanic_CorruptCode};
  return script_null();

ExecFailed:
  ctx->panic = (ScriptPanic){.kind = ScriptPanic_ExecutionFailed};
  return script_null();
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

void script_vm_disasm_write(const ScriptDoc* doc, const String code, DynString* out) {
  (void)doc;
  const u8* ip    = mem_begin(code);
  const u8* ipEnd = mem_end(code);
  while (ip != ipEnd) {
    // clang-format off
    switch ((ScriptOp)*ip) {
    case ScriptOp_Fail: {
      if (UNLIKELY((ip += 1) > ipEnd)) { return; }
      fmt_write(out, "[Fail]\n");
    } break;
    case ScriptOp_Return: {
      if (UNLIKELY((ip += 2) > ipEnd)) { return; }
      fmt_write(out, "[Return r{}]\n", fmt_int(ip[-1]));
    } break;
    case ScriptOp_Move: {
      if (UNLIKELY((ip += 3) > ipEnd)) { return; }
      fmt_write(out, "[Move r{} r{}]\n", fmt_int(ip[-2]), fmt_int(ip[-1]));
    } break;
    case ScriptOp_Value: {
      if (UNLIKELY((ip += 3) > ipEnd)) { return; }
      fmt_write(out, "[Value r{} v{}]\n", fmt_int(ip[-2]), fmt_int(ip[-1]));
    } break;
    case ScriptOp_Add: {
      if (UNLIKELY((ip += 3) > ipEnd)) { return; }
      fmt_write(out, "[Add r{} r{}]\n", fmt_int(ip[-2]), fmt_int(ip[-1]));
    } break;
    default:
      return;
    }
    // clang-format on
  }
}

String script_vm_disasm_scratch(const ScriptDoc* doc, const String code) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte * 16, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_vm_disasm_write(doc, code, &buffer);

  return dynstring_view(&buffer);
}
