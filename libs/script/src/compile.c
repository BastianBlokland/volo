#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "script_compile.h"
#include "script_vm.h"

#include "doc_internal.h"

static const String g_compileErrorStrs[] = {
    [ScriptCompileError_None]          = string_static("None"),
    [ScriptCompileError_TooManyValues] = string_static("Too many values"),
};
ASSERT(array_elems(g_compileErrorStrs) == ScriptCompileError_Count, "Incorrect number of strings");

typedef u8 RegId;

typedef struct {
  const ScriptDoc* doc;
  DynString*       out;
  u64              regAvailability; // Bitmask of available registers.
} CompileContext;

static u32 reg_available(CompileContext* ctx) { return bits_popcnt_64(ctx->regAvailability); }

static RegId reg_alloc(CompileContext* ctx) {
  if (UNLIKELY(!ctx->regAvailability)) {
    return sentinel_u8; // No registers are available.
  }
  const RegId res = bits_ctz_64(ctx->regAvailability);
  ctx->regAvailability &= ~(u64_lit(1) << res);
  return res;
}

static void reg_free(CompileContext* ctx, const RegId reg) {
  diag_assert_msg(!(ctx->regAvailability & (u64_lit(1) << reg)), "Register already freed");
  ctx->regAvailability |= u64_lit(1) << reg;
}

static void reg_free_all(CompileContext* ctx) {
  ASSERT(script_vm_regs <= 63, "Register allocator only supports up to 63 registers");
  ctx->regAvailability = (u64_lit(1) << script_vm_regs) - 1;
}

static void emit_fail(CompileContext* ctx) { dynstring_append_char(ctx->out, ScriptOp_Fail); }

static void emit_return(CompileContext* ctx, const RegId src) {
  diag_assert(src < script_vm_regs);
  dynstring_append_char(ctx->out, ScriptOp_Return);
  dynstring_append_char(ctx->out, src);
}

// static void emit_move(CompileContext* ctx, const RegId dst, const RegId src) {
//   diag_assert(dst < script_vm_regs && src < script_vm_regs);
//   if (dst != src) {
//     dynstring_append_char(ctx->out, ScriptOp_Move);
//     dynstring_append_char(ctx->out, dst);
//     dynstring_append_char(ctx->out, src);
//   }
// }

static void emit_value(CompileContext* ctx, const RegId dst, const u8 valId) {
  diag_assert(dst < script_vm_regs);
  dynstring_append_char(ctx->out, ScriptOp_Value);
  dynstring_append_char(ctx->out, dst);
  dynstring_append_char(ctx->out, valId);
}

static void emit_add(CompileContext* ctx, const RegId dst, const RegId a, const RegId b) {
  diag_assert(dst < script_vm_regs && a < script_vm_regs && b < script_vm_regs);
  dynstring_append_char(ctx->out, ScriptOp_Add);
  dynstring_append_char(ctx->out, dst);
  dynstring_append_char(ctx->out, a);
  dynstring_append_char(ctx->out, b);
}

static ScriptCompileError compile_expr(CompileContext*, RegId dst, ScriptExpr);

static ScriptCompileError compile_value(CompileContext* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprValue* data = &expr_data(ctx->doc, e)->value;
  if (data->valId > u8_max) {
    return ScriptCompileError_TooManyValues;
  }
  emit_value(ctx, dst, (u8)data->valId);
  return ScriptCompileError_None;
}

static ScriptCompileError compile_intr(CompileContext* ctx, const RegId dst, const ScriptExpr e) {
  const ScriptExprIntrinsic* data = &expr_data(ctx->doc, e)->intrinsic;
  const ScriptExpr*          args = expr_set_data(ctx->doc, data->argSet);
  switch (data->intrinsic) {
  case ScriptIntrinsic_Continue:
  case ScriptIntrinsic_Break:
  case ScriptIntrinsic_Return:
  case ScriptIntrinsic_Type:
  case ScriptIntrinsic_Hash:
  case ScriptIntrinsic_Assert:
  case ScriptIntrinsic_MemLoadDynamic:
  case ScriptIntrinsic_MemStoreDynamic:
  case ScriptIntrinsic_Select:
  case ScriptIntrinsic_NullCoalescing:
  case ScriptIntrinsic_LogicAnd:
  case ScriptIntrinsic_LogicOr:
  case ScriptIntrinsic_Loop:
  case ScriptIntrinsic_Equal:
  case ScriptIntrinsic_NotEqual:
  case ScriptIntrinsic_Less:
  case ScriptIntrinsic_LessOrEqual:
  case ScriptIntrinsic_Greater:
  case ScriptIntrinsic_GreaterOrEqual:
  case ScriptIntrinsic_Add: {
    compile_expr(ctx, dst, args[0]);
    const RegId tmpReg = reg_alloc(ctx);
    compile_expr(ctx, tmpReg, args[1]);
    emit_add(ctx, dst, dst, tmpReg);
    reg_free(ctx, tmpReg);
    return ScriptCompileError_None;
  }
  case ScriptIntrinsic_Sub:
  case ScriptIntrinsic_Mul:
  case ScriptIntrinsic_Div:
  case ScriptIntrinsic_Mod:
  case ScriptIntrinsic_Negate:
  case ScriptIntrinsic_Invert:
  case ScriptIntrinsic_Distance:
  case ScriptIntrinsic_Angle:
  case ScriptIntrinsic_Sin:
  case ScriptIntrinsic_Cos:
  case ScriptIntrinsic_Normalize:
  case ScriptIntrinsic_Magnitude:
  case ScriptIntrinsic_Absolute:
  case ScriptIntrinsic_VecX:
  case ScriptIntrinsic_VecY:
  case ScriptIntrinsic_VecZ:
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
    emit_fail(ctx);
    return ScriptCompileError_None;
  case ScriptIntrinsic_Count:
    break;
  }
  diag_assert_fail("Unknown intrinsic");
  UNREACHABLE
}

static ScriptCompileError compile_expr(CompileContext* ctx, const RegId dst, const ScriptExpr e) {
  switch (expr_kind(ctx->doc, e)) {
  case ScriptExprKind_Value:
    return compile_value(ctx, dst, e);
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_VarStore:
  case ScriptExprKind_MemLoad:
  case ScriptExprKind_MemStore:
  case ScriptExprKind_Intrinsic:
    return compile_intr(ctx, dst, e);
  case ScriptExprKind_Block:
  case ScriptExprKind_Extern:
    emit_fail(ctx);
    return ScriptCompileError_None;
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
  CompileContext ctx = {
      .doc = doc,
      .out = out,
  };
  reg_free_all(&ctx);
  diag_assert(reg_available(&ctx) == script_vm_regs);

  const RegId resultReg = reg_alloc(&ctx);
  diag_assert(resultReg < script_vm_regs);

  const ScriptCompileError err = compile_expr(&ctx, resultReg, expr);
  if (!err) {
    emit_return(&ctx, resultReg);
  }

  reg_free(&ctx, resultReg);
  diag_assert_msg(reg_available(&ctx) == script_vm_regs, "Not all registers freed");

  return err;
}
