#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "script_binder.h"
#include "script_error.h"
#include "script_mem.h"
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

INLINE_HINT static bool vm_reg_set_valid(ScriptVmContext* ctx, const u8 regId, const u8 regCount) {
  return regId + regCount <= array_elems(ctx->regs);
}

INLINE_HINT static bool vm_val_valid(ScriptVmContext* ctx, const u8 valId) {
  return valId < ctx->doc->values.size;
}

INLINE_HINT static u16 vm_read_u16(const u8 data[]) {
  // NOTE: Input data is not required to be aligned to 16 bit.
  return (u16)data[0] | (u16)data[1] << 8;
}

INLINE_HINT static u32 vm_read_u32(const u8 data[]) {
  // NOTE: Input data is not required to be aligned to 32 bit.
  return (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
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
    case ScriptOp_Return:
      if (UNLIKELY((ip += 2) > ipEnd)) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;
      return ctx->regs[ip[-1]];
    case ScriptOp_Move:
      if (UNLIKELY((ip += 3) >= ipEnd)) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-2]))) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;
      ctx->regs[ip[-2]] = ctx->regs[ip[-1]];
      continue;
    case ScriptOp_Value:
      if (UNLIKELY((ip += 3) >= ipEnd)) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-2]))) goto Corrupt;
      if (UNLIKELY(!vm_val_valid(ctx, ip[-1]))) goto Corrupt;
      ctx->regs[ip[-2]] = dynarray_begin_t(&ctx->doc->values, ScriptVal)[ip[-1]];
      continue;
    case ScriptOp_MemLoad:
      if (UNLIKELY((ip += 6) >= ipEnd)) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-5]))) goto Corrupt;
      ctx->regs[ip[-5]] = script_mem_load(ctx->m, vm_read_u32(&ip[-4]));
      continue;
    case ScriptOp_MemStore:
      if (UNLIKELY((ip += 6) >= ipEnd)) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-5]))) goto Corrupt;
      script_mem_store(ctx->m, vm_read_u32(&ip[-4]), ctx->regs[ip[-5]]);
      continue;
    case ScriptOp_Extern: {
      if (UNLIKELY((ip += 6) >= ipEnd)) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-5]))) goto Corrupt;
      if (UNLIKELY(!vm_reg_set_valid(ctx, ip[-2], ip[-1]))) goto Corrupt;
      const ScriptBinderSlot funcSlot = vm_read_u16(&ip[-4]);
      if (UNLIKELY(funcSlot >= script_binder_count(ctx->binder))) goto Corrupt;
      const ScriptArgs args = {.values = &ctx->regs[ip[-2]], .count = ip[-1]};
      ScriptError      err  = {0};
      ctx->regs[ip[-5]] = script_binder_exec(ctx->binder, funcSlot, ctx->bindCtx, args, &err);
      if (UNLIKELY(err.kind)) {
        ctx->panic = (ScriptPanic){.kind = script_error_to_panic(err.kind)};
        return script_null();
      }
    } continue;
#define OP_BINARY_SIMPLE(_OP_, _FUNC_)                                                             \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 3) >= ipEnd)) goto Corrupt;                                              \
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-2]))) goto Corrupt;                                      \
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;                                      \
      ctx->regs[ip[-2]] = _FUNC_(ctx->regs[ip[-2]], ctx->regs[ip[-1]]);                            \
      continue;
#define OP_UNARY_SIMPLE(_OP_, _FUNC_)                                                              \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) >= ipEnd)) goto Corrupt;                                              \
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;                                      \
      ctx->regs[ip[-1]] = _FUNC_(ctx->regs[ip[-1]]);                                               \
      continue;                                                                                    \

    OP_BINARY_SIMPLE(Add, script_val_add)
    OP_BINARY_SIMPLE(Sub, script_val_sub)
    OP_BINARY_SIMPLE(Mul, script_val_mul)
    OP_BINARY_SIMPLE(Div, script_val_div)
    OP_BINARY_SIMPLE(Mod, script_val_mod)
    OP_UNARY_SIMPLE(Negate, script_val_neg)
    OP_UNARY_SIMPLE(Invert, script_val_inv)

#undef OP_BINARY_SIMPLE
#undef OP_UNARY_SIMPLE
    }
    goto Corrupt;
  }
  // clang-format on

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
    case ScriptOp_Fail:
      if (UNLIKELY((ip += 1) > ipEnd)) return;
      fmt_write(out, "[Fail]\n");
      break;
    case ScriptOp_Return:
      if (UNLIKELY((ip += 2) > ipEnd)) return;
      fmt_write(out, "[Return r{}]\n", fmt_int(ip[-1]));
      break;
    case ScriptOp_Move:
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      fmt_write(out, "[Move r{} r{}]\n", fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
    case ScriptOp_Value:
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      fmt_write(out, "[Value r{} v{}]\n", fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
    case ScriptOp_MemLoad:
      if (UNLIKELY((ip += 6) > ipEnd)) return;
      fmt_write(out, "[MemLoad r{} #{}]\n", fmt_int(ip[-5]), fmt_int(vm_read_u32(&ip[-4])));
      break;
    case ScriptOp_MemStore:
      if (UNLIKELY((ip += 6) > ipEnd)) return;
      fmt_write(out, "[MemStore r{} #{}]\n", fmt_int(ip[-5]), fmt_int(vm_read_u32(&ip[-4])));
      break;
    case ScriptOp_Extern:
      if (UNLIKELY((ip += 6) > ipEnd)) return;
      fmt_write(out, "[Extern r{} f{} r{} c{}]\n", fmt_int(ip[-5]), fmt_int(vm_read_u16(&ip[-4])), fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
#define OP_BINARY_SIMPLE(_OP_)                                                                     \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 3) > ipEnd)) return;                                                     \
      fmt_write(out, "[" #_OP_ " r{} r{}]\n", fmt_int(ip[-2]), fmt_int(ip[-1]));                   \
      break;
#define OP_UNARY_SIMPLE(_OP_)                                                                      \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return;                                                     \
      fmt_write(out, "[" #_OP_ " r{}]\n", fmt_int(ip[-1]));                                        \
      break;

    OP_BINARY_SIMPLE(Add)
    OP_BINARY_SIMPLE(Sub)
    OP_BINARY_SIMPLE(Mul)
    OP_BINARY_SIMPLE(Div)
    OP_BINARY_SIMPLE(Mod)
    OP_UNARY_SIMPLE(Negate)
    OP_UNARY_SIMPLE(Invert)

#undef OP_BINARY_SIMPLE
#undef OP_UNARY_SIMPLE
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
