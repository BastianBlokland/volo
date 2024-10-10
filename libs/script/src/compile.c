#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "script_compile.h"
#include "script_vm.h"

#include "doc_internal.h"

static const String g_compileResStrs[] = {
    [ScriptCompileResult_Success]       = string_static("Success"),
    [ScriptCompileResult_TooManyValues] = string_static("Too many values"),
};
ASSERT(array_elems(g_compileResStrs) == ScriptCompileResult_Count, "Incorrect number of strings");

typedef enum {
  ScriptReg_Accum = 0,
  ScriptReg_GP0   = 1,
  ScriptReg_GP1   = 2,
} ScriptReg;

typedef struct {
  const ScriptDoc* doc;
  DynString*       out;
} ScriptCompileContext;

static void emit_fail(ScriptCompileContext* ctx) { dynstring_append_char(ctx->out, ScriptOp_Fail); }

static void emit_return(ScriptCompileContext* ctx, const ScriptReg src) {
  diag_assert(src < script_vm_regs);
  dynstring_append_char(ctx->out, ScriptOp_Return);
  dynstring_append_char(ctx->out, src);
}

static void emit_move(ScriptCompileContext* ctx, const ScriptReg dst, const ScriptReg src) {
  diag_assert(dst < script_vm_regs && src < script_vm_regs);
  if (dst != src) {
    dynstring_append_char(ctx->out, ScriptOp_Move);
    dynstring_append_char(ctx->out, dst);
    dynstring_append_char(ctx->out, src);
  }
}

static void emit_value(ScriptCompileContext* ctx, const ScriptReg dst, const u8 valId) {
  diag_assert(dst < script_vm_regs);
  dynstring_append_char(ctx->out, ScriptOp_Value);
  dynstring_append_char(ctx->out, dst);
  dynstring_append_char(ctx->out, valId);
}

static void emit_add(ScriptCompileContext* ctx, const ScriptReg src) {
  diag_assert(src < script_vm_regs);
  dynstring_append_char(ctx->out, ScriptOp_Add);
  dynstring_append_char(ctx->out, src);
}

static ScriptCompileResult compile_expr(ScriptCompileContext*, ScriptExpr, ScriptReg);

static ScriptCompileResult
compile_value(ScriptCompileContext* ctx, const ScriptReg dst, const ScriptExpr e) {
  const ScriptExprValue* data = &expr_data(ctx->doc, e)->value;
  if (data->valId > u8_max) {
    return ScriptCompileResult_TooManyValues;
  }
  emit_value(ctx, dst, (u8)data->valId);
  return ScriptCompileResult_Success;
}

static ScriptCompileResult
compile_intr(ScriptCompileContext* ctx, const ScriptReg dst, const ScriptExpr e) {
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
  case ScriptIntrinsic_Add:
    compile_expr(ctx, ScriptReg_GP0, args[0]);
    compile_expr(ctx, ScriptReg_GP1, args[1]);
    emit_move(ctx, ScriptReg_Accum, ScriptReg_GP0);
    emit_add(ctx, ScriptReg_GP1);
    emit_move(ctx, dst, ScriptReg_Accum);
    return ScriptCompileResult_Success;
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
    return ScriptCompileResult_Success;
  case ScriptIntrinsic_Count:
    break;
  }
  diag_assert_fail("Unknown intrinsic");
  UNREACHABLE
}

static ScriptCompileResult
compile_expr(ScriptCompileContext* ctx, const ScriptReg dst, const ScriptExpr e) {
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
    return ScriptCompileResult_Success;
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

String script_compile_result_str(const ScriptCompileResult res) {
  diag_assert(res < ScriptCompileResult_Count);
  return g_compileResStrs[res];
}

ScriptCompileResult script_compile(const ScriptDoc* doc, const ScriptExpr expr, DynString* out) {
  ScriptCompileContext ctx = {
      .doc = doc,
      .out = out,
  };
  const ScriptCompileResult res = compile_expr(&ctx, ScriptReg_Accum, expr);
  if (res == ScriptCompileResult_Success) {
    emit_return(&ctx, ScriptReg_Accum);
  }
  return res;
}
