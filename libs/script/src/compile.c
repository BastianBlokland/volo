#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "script_compile.h"
#include "script_vm.h"

#include "doc_internal.h"

ASSERT(script_vm_regs <= 63, "Register allocator only supports up to 63 registers");

static const String g_compileErrorStrs[] = {
    [ScriptCompileError_None]             = string_static("None"),
    [ScriptCompileError_TooManyRegisters] = string_static("Register limit exceeded"),
    [ScriptCompileError_TooManyValues]    = string_static("Value limit exceeded"),
};
ASSERT(array_elems(g_compileErrorStrs) == ScriptCompileError_Count, "Incorrect number of strings");

typedef u8 RegId;

typedef struct {
  RegId begin;
  u8    count;
} RegSet;

typedef struct {
  const ScriptDoc* doc;
  DynString*       out;
  ScriptOp         lastOp;
  u64              regAvailability; // Bitmask of available registers.
  RegId            varRegisters[script_var_count];
} Context;

static u32 reg_available(Context* ctx) { return bits_popcnt_64(ctx->regAvailability); }

static RegId reg_alloc(Context* ctx) {
  if (UNLIKELY(!ctx->regAvailability)) {
    return sentinel_u8; // No registers are available.
  }
  const RegId res = bits_ctz_64(ctx->regAvailability);
  ctx->regAvailability &= ~(u64_lit(1) << res);
  return res;
}

static RegSet reg_alloc_set(Context* ctx, const u8 count) {
  if (!count) {
    return (RegSet){0};
  }
  if (count > script_vm_regs) {
    return (RegSet){.begin = sentinel_u8, .count = sentinel_u8}; // Too many registers requested.
  }
  const u32 maxIndex = script_vm_regs - count;
  u64       mask     = (u64_lit(1) << count) - 1;
  for (u32 i = 0; i != maxIndex; ++i, mask <<= 1) {
    if ((ctx->regAvailability & mask) == mask) {
      return (RegSet){.begin = (RegId)i, .count = count};
    }
  }
  return (RegSet){.begin = sentinel_u8, .count = sentinel_u8}; // Not enough registers available.
}

static void reg_free(Context* ctx, const RegId reg) {
  diag_assert(reg < script_vm_regs);
  diag_assert_msg(!(ctx->regAvailability & (u64_lit(1) << reg)), "Register already freed");
  ctx->regAvailability |= u64_lit(1) << reg;
}

static void reg_free_set(Context* ctx, const RegSet set) {
  if (set.count) {
    const u8  last = set.begin + set.count - 1;
    const u64 mask = (u64_lit(1) << last) - (u64_lit(1) << set.begin);
    diag_assert_msg(!(ctx->regAvailability & mask), "Register already freed");
    ctx->regAvailability |= mask;
  }
}

static void reg_free_all(Context* ctx) {
  ctx->regAvailability = (u64_lit(1) << script_vm_regs) - 1;
}

static void emit_op(Context* ctx, const ScriptOp op) {
  dynstring_append_char(ctx->out, op);
  ctx->lastOp = op;
}

static void emit_value(Context* ctx, const RegId dst, const u8 valId) {
  diag_assert(dst < script_vm_regs && valId < ctx->doc->values.size);
  emit_op(ctx, ScriptOp_Value);
  dynstring_append_char(ctx->out, dst);
  dynstring_append_char(ctx->out, valId);
}

static void emit_unary(Context* ctx, const ScriptOp op, const RegId dst) {
  diag_assert(dst < script_vm_regs);
  emit_op(ctx, op);
  dynstring_append_char(ctx->out, dst);
}

static void emit_binary(Context* ctx, const ScriptOp op, const RegId dst, const RegId src) {
  diag_assert(dst < script_vm_regs && src < script_vm_regs);
  emit_op(ctx, op);
  dynstring_append_char(ctx->out, dst);
  dynstring_append_char(ctx->out, src);
}

static void emit_mem_op(Context* ctx, const ScriptOp op, const RegId dst, const StringHash key) {
  diag_assert((op == ScriptOp_MemLoad || op == ScriptOp_MemStore) && dst < script_vm_regs);
  emit_op(ctx, op);
  dynstring_append_char(ctx->out, dst);
  mem_write_le_u32(dynstring_push(ctx->out, 4), key);
}

static void emit_move(Context* ctx, const RegId dst, const RegId src) {
  if (dst != src) {
    emit_binary(ctx, ScriptOp_Move, dst, src);
  }
}

static void emit_extern(Context* ctx, const RegId dst, const ScriptBinderSlot f, const RegSet in) {
  diag_assert(dst < script_vm_regs && in.begin + in.count <= script_vm_regs);
  emit_op(ctx, ScriptOp_Extern);
  dynstring_append_char(ctx->out, dst);
  mem_write_le_u16(dynstring_push(ctx->out, 2), f);
  dynstring_append_char(ctx->out, in.begin);
  dynstring_append_char(ctx->out, in.count);
}

static ScriptCompileError compile_expr(Context*, RegId dst, ScriptExpr);

static ScriptCompileError compile_value(Context* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprValue* data = &expr_data(ctx->doc, e)->value;
  if (UNLIKELY(data->valId > u8_max)) {
    return ScriptCompileError_TooManyValues;
  }
  emit_value(ctx, dst, (u8)data->valId);
  return ScriptCompileError_None;
}

static ScriptCompileError compile_var_load(Context* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprVarLoad* data = &expr_data(ctx->doc, e)->var_load;
  diag_assert(!sentinel_check(ctx->varRegisters[data->var]));
  emit_move(ctx, dst, ctx->varRegisters[data->var]);
  return ScriptCompileError_None;
}

static ScriptCompileError compile_var_store(Context* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprVarStore* data = &expr_data(ctx->doc, e)->var_store;
  ScriptCompileError        err  = ScriptCompileError_None;
  if ((err = compile_expr(ctx, dst, data->val))) {
    return err;
  }
  if (sentinel_check(ctx->varRegisters[data->var])) {
    const RegId newReg = reg_alloc(ctx);
    if (UNLIKELY(sentinel_check(newReg))) {
      return ScriptCompileError_TooManyRegisters;
    }
    ctx->varRegisters[data->var] = newReg;
  }
  emit_move(ctx, ctx->varRegisters[data->var], dst);
  return ScriptCompileError_None;
}

static ScriptCompileError compile_mem_load(Context* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprMemLoad* data = &expr_data(ctx->doc, e)->mem_load;
  emit_mem_op(ctx, ScriptOp_MemLoad, dst, data->key);
  return ScriptCompileError_None;
}

static ScriptCompileError compile_mem_store(Context* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprMemStore* data = &expr_data(ctx->doc, e)->mem_store;
  ScriptCompileError        err  = ScriptCompileError_None;
  if ((err = compile_expr(ctx, dst, data->val))) {
    return err;
  }
  emit_mem_op(ctx, ScriptOp_MemStore, dst, data->key);
  return err;
}

static ScriptCompileError
compile_intr_unary(Context* ctx, const RegId dst, const ScriptOp op, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, dst, args[0]))) {
    return err;
  }
  emit_unary(ctx, op, dst);
  return err;
}

static ScriptCompileError
compile_intr_binary(Context* ctx, const RegId dst, const ScriptOp op, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, dst, args[0]))) {
    return err;
  }
  const RegId tmpReg = reg_alloc(ctx);
  if (sentinel_check(tmpReg)) {
    err = ScriptCompileError_TooManyRegisters;
    return err;
  }
  if ((err = compile_expr(ctx, tmpReg, args[1]))) {
    return err;
  }
  emit_binary(ctx, op, dst, tmpReg);
  reg_free(ctx, tmpReg);
  return err;
}

static ScriptCompileError compile_intr(Context* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprIntrinsic* data = &expr_data(ctx->doc, e)->intrinsic;
  const ScriptExpr*          args = expr_set_data(ctx->doc, data->argSet);
  switch (data->intrinsic) {
  case ScriptIntrinsic_Continue:
  case ScriptIntrinsic_Break:
    emit_op(ctx, ScriptOp_Fail);
    return ScriptCompileError_None;
  case ScriptIntrinsic_Return:
    return compile_intr_unary(ctx, dst, ScriptOp_Return, args);
  case ScriptIntrinsic_Type:
    return compile_intr_unary(ctx, dst, ScriptOp_Type, args);
  case ScriptIntrinsic_Hash:
    return compile_intr_unary(ctx, dst, ScriptOp_Hash, args);
  case ScriptIntrinsic_Assert:
    return compile_intr_unary(ctx, dst, ScriptOp_Assert, args);
  case ScriptIntrinsic_MemLoadDynamic:
    return compile_intr_unary(ctx, dst, ScriptOp_MemLoadDyn, args);
  case ScriptIntrinsic_MemStoreDynamic:
    return compile_intr_binary(ctx, dst, ScriptOp_MemStoreDyn, args);
  case ScriptIntrinsic_Select:
  case ScriptIntrinsic_NullCoalescing:
  case ScriptIntrinsic_LogicAnd:
  case ScriptIntrinsic_LogicOr:
  case ScriptIntrinsic_Loop:
    emit_op(ctx, ScriptOp_Fail);
    return ScriptCompileError_None;
  case ScriptIntrinsic_Equal:
    return compile_intr_binary(ctx, dst, ScriptOp_Equal, args);
  case ScriptIntrinsic_NotEqual: {
    const ScriptCompileError err = compile_intr_binary(ctx, dst, ScriptOp_Equal, args);
    if (!err) {
      emit_unary(ctx, ScriptOp_Invert, dst);
    }
    return err;
  }
  case ScriptIntrinsic_Less:
    return compile_intr_binary(ctx, dst, ScriptOp_Less, args);
  case ScriptIntrinsic_LessOrEqual: {
    const ScriptCompileError err = compile_intr_binary(ctx, dst, ScriptOp_Greater, args);
    if (!err) {
      emit_unary(ctx, ScriptOp_Invert, dst);
    }
    return err;
  }
  case ScriptIntrinsic_Greater:
    return compile_intr_binary(ctx, dst, ScriptOp_Greater, args);
  case ScriptIntrinsic_GreaterOrEqual: {
    const ScriptCompileError err = compile_intr_binary(ctx, dst, ScriptOp_Less, args);
    if (!err) {
      emit_unary(ctx, ScriptOp_Invert, dst);
    }
    return err;
  }
  case ScriptIntrinsic_Add:
    return compile_intr_binary(ctx, dst, ScriptOp_Add, args);
  case ScriptIntrinsic_Sub:
    return compile_intr_binary(ctx, dst, ScriptOp_Sub, args);
  case ScriptIntrinsic_Mul:
    return compile_intr_binary(ctx, dst, ScriptOp_Mul, args);
  case ScriptIntrinsic_Div:
    return compile_intr_binary(ctx, dst, ScriptOp_Div, args);
  case ScriptIntrinsic_Mod:
    return compile_intr_binary(ctx, dst, ScriptOp_Mod, args);
  case ScriptIntrinsic_Negate:
    return compile_intr_unary(ctx, dst, ScriptOp_Negate, args);
  case ScriptIntrinsic_Invert:
    return compile_intr_unary(ctx, dst, ScriptOp_Invert, args);
  case ScriptIntrinsic_Distance:
    return compile_intr_binary(ctx, dst, ScriptOp_Distance, args);
  case ScriptIntrinsic_Angle:
    return compile_intr_binary(ctx, dst, ScriptOp_Angle, args);
  case ScriptIntrinsic_Sin:
    return compile_intr_unary(ctx, dst, ScriptOp_Sin, args);
  case ScriptIntrinsic_Cos:
    return compile_intr_unary(ctx, dst, ScriptOp_Cos, args);
  case ScriptIntrinsic_Normalize:
    return compile_intr_unary(ctx, dst, ScriptOp_Normalize, args);
  case ScriptIntrinsic_Magnitude:
    return compile_intr_unary(ctx, dst, ScriptOp_Magnitude, args);
  case ScriptIntrinsic_Absolute:
    return compile_intr_unary(ctx, dst, ScriptOp_Absolute, args);
  case ScriptIntrinsic_VecX:
    return compile_intr_unary(ctx, dst, ScriptOp_VecX, args);
  case ScriptIntrinsic_VecY:
    return compile_intr_unary(ctx, dst, ScriptOp_VecY, args);
  case ScriptIntrinsic_VecZ:
    return compile_intr_unary(ctx, dst, ScriptOp_VecZ, args);
  case ScriptIntrinsic_Vec3Compose:
  case ScriptIntrinsic_QuatFromEuler:
  case ScriptIntrinsic_QuatFromAngleAxis:
  case ScriptIntrinsic_ColorCompose:
  case ScriptIntrinsic_ColorComposeHsv:
  case ScriptIntrinsic_ColorFor:
  case ScriptIntrinsic_Random:
  case ScriptIntrinsic_RandomSphere:
  case ScriptIntrinsic_RandomCircleXZ:
  case ScriptIntrinsic_RandomBetween:
  case ScriptIntrinsic_RoundDown:
  case ScriptIntrinsic_RoundNearest:
  case ScriptIntrinsic_RoundUp:
  case ScriptIntrinsic_Clamp:
  case ScriptIntrinsic_Lerp:
  case ScriptIntrinsic_Min:
  case ScriptIntrinsic_Max:
  case ScriptIntrinsic_Perlin3:
    emit_op(ctx, ScriptOp_Fail);
    return ScriptCompileError_None;
  case ScriptIntrinsic_Count:
    break;
  }
  diag_assert_fail("Unknown intrinsic");
  UNREACHABLE
}

static ScriptCompileError compile_block(Context* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprBlock* data  = &expr_data(ctx->doc, e)->block;
  const ScriptExpr*      exprs = expr_set_data(ctx->doc, data->exprSet);

  // NOTE: Blocks need at least one expression.
  ScriptCompileError err = ScriptCompileError_None;
  for (u32 i = 0; i != data->exprCount; ++i) {
    if ((err = compile_expr(ctx, dst, exprs[i]))) {
      break;
    }
  }
  return err;
}

static ScriptCompileError compile_extern(Context* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprExtern* data     = &expr_data(ctx->doc, e)->extern_;
  const ScriptExpr*       argExprs = expr_set_data(ctx->doc, data->argSet);
  const RegSet            argRegs  = reg_alloc_set(ctx, data->argCount);
  if (sentinel_check(argRegs.begin)) {
    return ScriptCompileError_TooManyRegisters;
  }
  ScriptCompileError err = ScriptCompileError_None;
  for (u32 i = 0; i != data->argCount; ++i) {
    if ((err = compile_expr(ctx, argRegs.begin + i, argExprs[i]))) {
      goto Ret;
    }
  }
  emit_extern(ctx, dst, data->func, argRegs);
  reg_free_set(ctx, argRegs);
Ret:
  return err;
}

static ScriptCompileError compile_expr(Context* ctx, const RegId dst, const ScriptExpr e) {
  switch (expr_kind(ctx->doc, e)) {
  case ScriptExprKind_Value:
    return compile_value(ctx, dst, e);
  case ScriptExprKind_VarLoad:
    return compile_var_load(ctx, dst, e);
  case ScriptExprKind_VarStore:
    return compile_var_store(ctx, dst, e);
  case ScriptExprKind_MemLoad:
    return compile_mem_load(ctx, dst, e);
  case ScriptExprKind_MemStore:
    return compile_mem_store(ctx, dst, e);
  case ScriptExprKind_Intrinsic:
    return compile_intr(ctx, dst, e);
  case ScriptExprKind_Block:
    return compile_block(ctx, dst, e);
  case ScriptExprKind_Extern:
    return compile_extern(ctx, dst, e);
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

String script_compile_error_str(const ScriptCompileError res) {
  diag_assert(res < ScriptCompileError_Count);
  return g_compileErrorStrs[res];
}

ScriptCompileError script_compile(const ScriptDoc* doc, const ScriptExpr expr, DynString* out) {
  Context ctx = {
      .doc = doc,
      .out = out,
  };
  mem_set(array_mem(ctx.varRegisters), 0xFF);

  reg_free_all(&ctx);
  diag_assert(reg_available(&ctx) == script_vm_regs);

  const RegId resultReg = reg_alloc(&ctx);
  diag_assert(resultReg < script_vm_regs);

  const ScriptCompileError err = compile_expr(&ctx, resultReg, expr);
  if (!err) {
    if (ctx.lastOp != ScriptOp_Return) {
      emit_unary(&ctx, ScriptOp_Return, resultReg);
    }

    // Verify no registers where leaked.
    reg_free(&ctx, resultReg);
    array_for_t(ctx.varRegisters, RegId, varReg) {
      if (!sentinel_check(*varReg)) {
        reg_free(&ctx, *varReg);
      }
    }
    diag_assert_msg(reg_available(&ctx) == script_vm_regs, "Not all registers freed");
  }
  return err;
}
