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
    case ScriptOp_Fail:
      ctx->panic = (ScriptPanic){.kind = ScriptPanic_ExecutionFailed};
      return script_null();
    case ScriptOp_Assert:
      if (UNLIKELY((ip += 2) >= ipEnd)) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;
      if (script_falsy(ctx->regs[ip[-1]])) {
        ctx->panic = (ScriptPanic){.kind = ScriptPanic_AssertionFailed};
        return script_null();
      }
      continue;
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
    case ScriptOp_MemLoadDyn:
      if (UNLIKELY((ip += 2) >= ipEnd)) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;
      if(val_type(ctx->regs[ip[-1]]) == ScriptType_Str) {
        ctx->regs[ip[-1]] = script_mem_load(ctx->m, val_as_str(ctx->regs[ip[-1]]));
      } else {
        ctx->regs[ip[-1]] = val_null();
      }
      continue;
    case ScriptOp_MemStoreDyn:
      if (UNLIKELY((ip += 3) >= ipEnd)) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-2]))) goto Corrupt;
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;
      if(val_type(ctx->regs[ip[-2]]) == ScriptType_Str) {
        script_mem_store(ctx->m, val_as_str(ctx->regs[ip[-2]]), ctx->regs[ip[-1]]);
        ctx->regs[ip[-2]] = ctx->regs[ip[-1]];
      } else {
        ctx->regs[ip[-2]] = val_null();
      }
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
#define OP_SIMPLE_UNARY(_OP_, _FUNC_)                                                              \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) >= ipEnd)) goto Corrupt;                                              \
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;                                      \
      ctx->regs[ip[-1]] = _FUNC_(ctx->regs[ip[-1]]);                                               \
      continue
#define OP_SIMPLE_BINARY(_OP_, _FUNC_)                                                             \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 3) >= ipEnd)) goto Corrupt;                                              \
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-2]))) goto Corrupt;                                      \
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;                                      \
      ctx->regs[ip[-2]] = _FUNC_(ctx->regs[ip[-2]], ctx->regs[ip[-1]]);                            \
      continue
#define OP_SIMPLE_TERNARY(_OP_, _FUNC_)                                                            \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 4) >= ipEnd)) goto Corrupt;                                              \
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-3]))) goto Corrupt;                                      \
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-2]))) goto Corrupt;                                      \
      if (UNLIKELY(!vm_reg_valid(ctx, ip[-1]))) goto Corrupt;                                      \
      ctx->regs[ip[-3]] = _FUNC_(ctx->regs[ip[-3]], ctx->regs[ip[-2]], ctx->regs[ip[-1]]);         \
      continue

    OP_SIMPLE_UNARY(Type,          script_val_type);
    OP_SIMPLE_UNARY(Hash,          script_val_hash);
    OP_SIMPLE_BINARY(Equal,        script_val_equal_as_val);
    OP_SIMPLE_BINARY(Less,         script_val_less_as_val);
    OP_SIMPLE_BINARY(Greater,      script_val_greater_as_val);
    OP_SIMPLE_BINARY(Add,          script_val_add);
    OP_SIMPLE_BINARY(Sub,          script_val_sub);
    OP_SIMPLE_BINARY(Mul,          script_val_mul);
    OP_SIMPLE_BINARY(Div,          script_val_div);
    OP_SIMPLE_BINARY(Mod,          script_val_mod);
    OP_SIMPLE_UNARY(Negate,        script_val_neg);
    OP_SIMPLE_UNARY(Invert,        script_val_inv);
    OP_SIMPLE_BINARY(Distance,     script_val_dist);
    OP_SIMPLE_BINARY(Angle,        script_val_angle);
    OP_SIMPLE_UNARY(Sin,           script_val_sin);
    OP_SIMPLE_UNARY(Cos,           script_val_cos);
    OP_SIMPLE_UNARY(Normalize,     script_val_norm);
    OP_SIMPLE_UNARY(Magnitude,     script_val_mag);
    OP_SIMPLE_UNARY(Absolute,      script_val_abs);
    OP_SIMPLE_UNARY(VecX,          script_val_vec_x);
    OP_SIMPLE_UNARY(VecY,          script_val_vec_y);
    OP_SIMPLE_UNARY(VecZ,          script_val_vec_z);
    OP_SIMPLE_TERNARY(Vec3Compose, script_val_vec3_compose);

#undef OP_SIMPLE_TERNARY
#undef OP_SIMPLE_BINARY
#undef OP_SIMPLE_UNARY
    }
    goto Corrupt;
  }
  // clang-format on

Corrupt:
  ctx->panic = (ScriptPanic){.kind = ScriptPanic_CorruptCode};
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
    case ScriptOp_Assert:
      if (UNLIKELY((ip += 2) > ipEnd)) return;
      fmt_write(out, "[Assert r{}]\n", fmt_int(ip[-1]));
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
    case ScriptOp_MemLoadDyn:
      if (UNLIKELY((ip += 2) > ipEnd)) return;
      fmt_write(out, "[MemLoadDyn r{}]\n", fmt_int(ip[-1]));
      break;
    case ScriptOp_MemStoreDyn:
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      fmt_write(out, "[MemStoreDyn r{} r{}]\n", fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
    case ScriptOp_Extern:
      if (UNLIKELY((ip += 6) > ipEnd)) return;
      fmt_write(out, "[Extern r{} f{} r{} c{}]\n", fmt_int(ip[-5]), fmt_int(vm_read_u16(&ip[-4])), fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
#define OP_SIMPLE_UNARY(_OP_)                                                                      \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return;                                                     \
      fmt_write(out, "[" #_OP_ " r{}]\n", fmt_int(ip[-1]));                                        \
      break
#define OP_SIMPLE_BINARY(_OP_)                                                                     \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 3) > ipEnd)) return;                                                     \
      fmt_write(out, "[" #_OP_ " r{} r{}]\n", fmt_int(ip[-2]), fmt_int(ip[-1]));                   \
      break
#define OP_SIMPLE_TERNARY(_OP_)                                                                    \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 4) > ipEnd)) return;                                                     \
      fmt_write(out, "[" #_OP_ " r{} r{} r{}]\n",                                                  \
        fmt_int(ip[-3]), fmt_int(ip[-2]), fmt_int(ip[-1]));                                        \
      break

    OP_SIMPLE_UNARY(Type);
    OP_SIMPLE_UNARY(Hash);
    OP_SIMPLE_BINARY(Equal);
    OP_SIMPLE_BINARY(Less);
    OP_SIMPLE_BINARY(Greater);
    OP_SIMPLE_BINARY(Add);
    OP_SIMPLE_BINARY(Sub);
    OP_SIMPLE_BINARY(Mul);
    OP_SIMPLE_BINARY(Div);
    OP_SIMPLE_BINARY(Mod);
    OP_SIMPLE_UNARY(Negate);
    OP_SIMPLE_UNARY(Invert);
    OP_SIMPLE_BINARY(Distance);
    OP_SIMPLE_BINARY(Angle);
    OP_SIMPLE_UNARY(Sin);
    OP_SIMPLE_UNARY(Cos);
    OP_SIMPLE_UNARY(Normalize);
    OP_SIMPLE_UNARY(Magnitude);
    OP_SIMPLE_UNARY(Absolute);
    OP_SIMPLE_UNARY(VecX);
    OP_SIMPLE_UNARY(VecY);
    OP_SIMPLE_UNARY(VecZ);
    OP_SIMPLE_TERNARY(Vec3Compose);

#undef OP_SIMPLE_TERNARY
#undef OP_SIMPLE_BINARY
#undef OP_SIMPLE_UNARY
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
