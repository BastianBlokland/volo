#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_math.h"
#include "script_compile.h"
#include "script_intrinsic.h"
#include "script_prog.h"

#include "doc_internal.h"

ASSERT(script_prog_regs <= 63, "Register allocator only supports up to 63 registers");

static const String g_compileErrorStrs[] = {
    [ScriptCompileError_None]              = string_static("None"),
    [ScriptCompileError_TooManyRegisters]  = string_static("Register limit exceeded"),
    [ScriptCompileError_TooManyValues]     = string_static("Value limit exceeded"),
    [ScriptCompileError_CodeLimitExceeded] = string_static("Output exceeds 0xFFFF bytes"),
};
ASSERT(array_elems(g_compileErrorStrs) == ScriptCompileError_Count, "Incorrect number of strings");

typedef u8  RegId;
typedef u32 LabelId;

typedef struct {
  RegId begin;
  u8    count;
} RegSet;

typedef struct {
  u16 instruction; // Offset in the output stream.
} Label;

typedef struct {
  LabelId label;
  u16     offset; // Offset in the output stream.
} LabelPatch;

typedef struct {
  RegId reg;
  bool  optional;  // Value is not needed but the register can be used as a temporary.
  bool  condition; // Value is used as a condition, only truthy vs falsy matters.
} Target;

#define target_reg(_REG_)                                                                          \
  (Target) { .reg = (_REG_) }

#define target_reg_cond(_REG_)                                                                     \
  (Target) { .reg = (_REG_), .condition = true }

#define target_reg_opt(_REG_)                                                                      \
  (Target) { .reg = (_REG_), .optional = true }

typedef struct {
  const ScriptDoc*    doc;
  const ScriptLookup* lookup;
  DynString           outCode;
  DynArray            outLiterals;  // ScriptVal[].
  DynArray            outLocations; // ScriptProgramLoc[].
  ScriptOp            lastOp;

  u64   regAvailability; // Bitmask of available registers.
  RegId varRegisters[script_var_count];

  LabelId loopLabelIncrement, loopLabelEnd;

  DynArray labels;       // Label[].
  DynArray labelPatches; // LabelPatch[].
} Context;

static i8 script_compare_loc(const void* a, const void* b) {
  const ScriptProgramLoc* posA = a;
  const ScriptProgramLoc* posB = b;
  return compare_u16(&posA->instruction, &posB->instruction);
}

MAYBE_UNUSED static u32 reg_available(Context* ctx) { return bits_popcnt_64(ctx->regAvailability); }

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
  if (count > script_prog_regs) {
    return (RegSet){.begin = sentinel_u8, .count = sentinel_u8}; // Too many registers requested.
  }
  const u32 maxIndex = script_prog_regs - count;
  u64       mask     = (u64_lit(1) << count) - 1;
  for (u32 i = 0; i != maxIndex; ++i, mask <<= 1) {
    if ((ctx->regAvailability & mask) == mask) {
      ctx->regAvailability &= ~mask;
      return (RegSet){.begin = (RegId)i, .count = count};
    }
  }
  return (RegSet){.begin = sentinel_u8, .count = sentinel_u8}; // Not enough registers available.
}

static void reg_free(Context* ctx, const RegId reg) {
  diag_assert(reg < script_prog_regs);
  diag_assert_msg(!(ctx->regAvailability & (u64_lit(1) << reg)), "Register already freed");
  ctx->regAvailability |= u64_lit(1) << reg;
}

static void reg_free_set(Context* ctx, const RegSet set) {
  if (set.count) {
    const u8  end  = set.begin + set.count;
    const u64 mask = (u64_lit(1) << end) - (u64_lit(1) << set.begin);
    diag_assert_msg(!(ctx->regAvailability & mask), "Register already freed");
    ctx->regAvailability |= mask;
  }
}

static void reg_free_all(Context* ctx) {
  ctx->regAvailability = (u64_lit(1) << script_prog_regs) - 1;
}

static LabelId label_alloc(Context* ctx) {
  const LabelId res                     = (LabelId)ctx->labels.size;
  *dynarray_push_t(&ctx->labels, Label) = (Label){.instruction = 0xFFFF};
  return res;
}

static void label_link(Context* ctx, const LabelId labelId) {
  Label* label = dynarray_at_t(&ctx->labels, labelId, Label);
  diag_assert_msg(sentinel_check(label->instruction), "Label {} already linked", fmt_int(labelId));
  label->instruction = (u16)ctx->outCode.size;

  // If there are any outstanding patches for this label then apply them now.
  for (usize i = ctx->labelPatches.size; i--;) {
    LabelPatch* patch = dynarray_at_t(&ctx->labelPatches, i, LabelPatch);
    if (patch->label == labelId) {
      const String patchCode = mem_slice(dynstring_view(&ctx->outCode), patch->offset, 2);
      mem_write_le_u16(patchCode, label->instruction);
      dynarray_remove(&ctx->labelPatches, i, 1);
    }
  }
}

static void label_write(Context* ctx, const LabelId labelId) {
  Label* label = dynarray_at_t(&ctx->labels, labelId, Label);
  if (sentinel_check(label->instruction)) {
    // No instruction known yet for the label; register a pending patch.
    *dynarray_push_t(&ctx->labelPatches, LabelPatch) = (LabelPatch){
        .label  = labelId,
        .offset = (u16)ctx->outCode.size,
    };
    mem_write_le_u16(dynstring_push(&ctx->outCode, 2), 0xFFFF);
    return;
  }
  mem_write_le_u16(dynstring_push(&ctx->outCode, 2), label->instruction);
}

static void emit_location(Context* ctx, const ScriptExpr e) {
  if (ctx->outCode.size > u16_max) {
    return; // Code out of bounds (will result in a compile error).
  }
  if (!ctx->lookup) {
    return; // No lookup provided to resource source locations.
  }
  const ScriptRange range = script_expr_range(ctx->doc, e);
  if (sentinel_check(range.start) || sentinel_check(range.end)) {
    return; // Location unknown.
  }
  *dynarray_push_t(&ctx->outLocations, ScriptProgramLoc) = (ScriptProgramLoc){
      .instruction = (u16)ctx->outCode.size,
      .range       = script_lookup_range_to_line_col(ctx->lookup, range),
  };
}

static void emit_op(Context* ctx, const ScriptOp op) {
  dynstring_append_char(&ctx->outCode, op);
  ctx->lastOp = op;
}

static void emit_value(Context* ctx, const RegId dst, const u8 literalId) {
  diag_assert(dst < script_prog_regs && literalId < ctx->outLiterals.size);
  emit_op(ctx, ScriptOp_Value);
  dynstring_append_char(&ctx->outCode, dst);
  dynstring_append_char(&ctx->outCode, literalId);
}

static void emit_value_bool(Context* ctx, const RegId dst, const bool val) {
  diag_assert(dst < script_prog_regs);
  emit_op(ctx, ScriptOp_ValueBool);
  dynstring_append_char(&ctx->outCode, dst);
  dynstring_append_char(&ctx->outCode, val);
}

static void emit_value_small_int(Context* ctx, const RegId dst, const u8 val) {
  diag_assert(dst < script_prog_regs);
  emit_op(ctx, ScriptOp_ValueSmallInt);
  dynstring_append_char(&ctx->outCode, dst);
  dynstring_append_char(&ctx->outCode, val);
}

static void emit_unary(Context* ctx, const ScriptOp op, const RegId dst) {
  diag_assert(dst < script_prog_regs);
  emit_op(ctx, op);
  dynstring_append_char(&ctx->outCode, dst);
}

static void emit_binary(Context* ctx, const ScriptOp op, const RegId dst, const RegId src) {
  diag_assert(dst < script_prog_regs && src < script_prog_regs);
  emit_op(ctx, op);
  dynstring_append_char(&ctx->outCode, dst);
  dynstring_append_char(&ctx->outCode, src);
}

static void
emit_ternary(Context* ctx, const ScriptOp op, const RegId dst, const RegId src1, const RegId src2) {
  diag_assert(dst < script_prog_regs && src1 < script_prog_regs && src2 < script_prog_regs);
  emit_op(ctx, op);
  dynstring_append_char(&ctx->outCode, dst);
  dynstring_append_char(&ctx->outCode, src1);
  dynstring_append_char(&ctx->outCode, src2);
}

static void emit_quaternary(
    Context*       ctx,
    const ScriptOp op,
    const RegId    dst,
    const RegId    src1,
    const RegId    src2,
    const RegId    src3) {
  diag_assert(dst < script_prog_regs);
  diag_assert(src1 < script_prog_regs && src2 < script_prog_regs && src3 < script_prog_regs);
  emit_op(ctx, op);
  dynstring_append_char(&ctx->outCode, dst);
  dynstring_append_char(&ctx->outCode, src1);
  dynstring_append_char(&ctx->outCode, src2);
  dynstring_append_char(&ctx->outCode, src3);
}

static void emit_mem_op(Context* ctx, const ScriptOp op, const RegId dst, const StringHash key) {
  diag_assert((op == ScriptOp_MemLoad || op == ScriptOp_MemStore) && dst < script_prog_regs);
  emit_op(ctx, op);
  dynstring_append_char(&ctx->outCode, dst);
  mem_write_le_u32(dynstring_push(&ctx->outCode, 4), key);
}

static void emit_move(Context* ctx, const RegId dst, const RegId src) {
  if (dst != src) {
    emit_binary(ctx, ScriptOp_Move, dst, src);
  }
}

static void emit_jump(Context* ctx, const LabelId label) {
  emit_op(ctx, ScriptOp_Jump);
  label_write(ctx, label);
}

static void emit_jump_if_truthy(Context* ctx, const RegId cond, const LabelId label) {
  diag_assert(cond < script_prog_regs);
  emit_op(ctx, ScriptOp_JumpIfTruthy);
  dynstring_append_char(&ctx->outCode, cond);
  label_write(ctx, label);
}

static void emit_jump_if_falsy(Context* ctx, const RegId cond, const LabelId label) {
  diag_assert(cond < script_prog_regs);
  emit_op(ctx, ScriptOp_JumpIfFalsy);
  dynstring_append_char(&ctx->outCode, cond);
  label_write(ctx, label);
}

static void emit_jump_if_non_null(Context* ctx, const RegId cond, const LabelId label) {
  diag_assert(cond < script_prog_regs);
  emit_op(ctx, ScriptOp_JumpIfNonNull);
  dynstring_append_char(&ctx->outCode, cond);
  label_write(ctx, label);
}

static void emit_extern(Context* ctx, const RegId dst, const ScriptBinderSlot f, const RegSet in) {
  diag_assert(dst < script_prog_regs && in.begin + in.count <= script_prog_regs);
  emit_op(ctx, ScriptOp_Extern);
  dynstring_append_char(&ctx->outCode, dst);
  mem_write_le_u16(dynstring_push(&ctx->outCode, 2), f);
  dynstring_append_char(&ctx->outCode, in.begin);
  dynstring_append_char(&ctx->outCode, in.count);
}

static bool expr_is_null(Context* ctx, const ScriptExpr e) {
  if (expr_kind(ctx->doc, e) != ScriptExprKind_Value) {
    return false;
  }
  const ScriptExprValue* data = &expr_data(ctx->doc, e)->value;
  const ScriptVal        val  = *dynarray_at_t(&ctx->doc->values, data->valId, ScriptVal);
  return script_type(val) == ScriptType_Null;
}

static bool expr_is_true(Context* ctx, const ScriptExpr e) {
  if (expr_kind(ctx->doc, e) != ScriptExprKind_Value) {
    return false;
  }
  const ScriptExprValue* data = &expr_data(ctx->doc, e)->value;
  const ScriptVal        val  = *dynarray_at_t(&ctx->doc->values, data->valId, ScriptVal);
  return script_get_bool(val, false);
}

static bool expr_is_var_load(Context* ctx, const ScriptExpr e) {
  return expr_kind(ctx->doc, e) == ScriptExprKind_VarLoad;
}

static bool expr_is_intrinsic(Context* ctx, const ScriptExpr e, const ScriptIntrinsic intr) {
  if (expr_kind(ctx->doc, e) != ScriptExprKind_Intrinsic) {
    return false;
  }
  const ScriptExprIntrinsic* data = &expr_data(ctx->doc, e)->intrinsic;
  return data->intrinsic == intr;
}

static ScriptCompileError compile_expr(Context*, Target, ScriptExpr);

static ScriptCompileError compile_value(Context* ctx, const Target tgt, const ScriptExpr e) {
  if (tgt.optional) {
    return ScriptCompileError_None;
  }
  const ScriptExprValue* data = &expr_data(ctx->doc, e)->value;
  const ScriptVal        val  = *dynarray_at_t(&ctx->doc->values, data->valId, ScriptVal);
  switch (script_type(val)) {
  case ScriptType_Null:
    emit_unary(ctx, ScriptOp_ValueNull, tgt.reg);
    break;
  case ScriptType_Bool:
    emit_value_bool(ctx, tgt.reg, script_get_bool(val, false));
    break;
  case ScriptType_Num: {
    const f64 num     = script_get_num(val, 0.0);
    const f64 rounded = math_round_nearest_f64(num);
    if (num == rounded && rounded >= 0.0 && rounded <= u8_max) {
      emit_value_small_int(ctx, tgt.reg, (u8)rounded);
      break;
    }
  } // Fallthrough.
  default:
    if (UNLIKELY(ctx->outLiterals.size >= u8_max)) {
      return ScriptCompileError_TooManyValues;
    }
    const u8 literalId                             = (u8)ctx->outLiterals.size;
    *dynarray_push_t(&ctx->outLiterals, ScriptVal) = val;
    emit_value(ctx, tgt.reg, literalId);
    break;
  }
  return ScriptCompileError_None;
}

static ScriptCompileError compile_var_load(Context* ctx, const Target tgt, const ScriptExpr e) {
  const ScriptExprVarLoad* data = &expr_data(ctx->doc, e)->var_load;
  diag_assert(!sentinel_check(ctx->varRegisters[data->var]));
  if (!tgt.optional) {
    // NOTE: Optional variable load doesn't make sense and should produce warnings during read.
    emit_move(ctx, tgt.reg, ctx->varRegisters[data->var]);
  }
  return ScriptCompileError_None;
}

static ScriptCompileError compile_var_store(Context* ctx, const Target tgt, const ScriptExpr e) {
  const ScriptExprVarStore* data = &expr_data(ctx->doc, e)->var_store;
  ScriptCompileError        err  = ScriptCompileError_None;
  if (sentinel_check(ctx->varRegisters[data->var])) {
    const RegId newReg = reg_alloc(ctx);
    if (UNLIKELY(sentinel_check(newReg))) {
      err = ScriptCompileError_TooManyRegisters;
      return err;
    }
    ctx->varRegisters[data->var] = newReg;
  }
  if ((err = compile_expr(ctx, target_reg(ctx->varRegisters[data->var]), data->val))) {
    return err;
  }
  if (!tgt.optional) {
    emit_move(ctx, tgt.reg, ctx->varRegisters[data->var]); // Return the stored variable.
  }
  return ScriptCompileError_None;
}

static ScriptCompileError compile_mem_load(Context* ctx, const Target tgt, const ScriptExpr e) {
  const ScriptExprMemLoad* data = &expr_data(ctx->doc, e)->mem_load;
  if (!tgt.optional) {
    // NOTE: Optional memory load doesn't make sense and should produce warnings during read.
    emit_mem_op(ctx, ScriptOp_MemLoad, tgt.reg, data->key);
  }
  return ScriptCompileError_None;
}

static ScriptCompileError compile_mem_store(Context* ctx, const Target tgt, const ScriptExpr e) {
  const ScriptExprMemStore* data = &expr_data(ctx->doc, e)->mem_store;

  if (tgt.optional && expr_is_var_load(ctx, data->val)) {
    /**
     * Fast path for storing a variable to memory when the target register is optional; We can skip
     * the load and instead directly store the variable register.
     */
    const ScriptExprVarLoad* varLoadData = &expr_data(ctx->doc, data->val)->var_load;
    diag_assert(!sentinel_check(ctx->varRegisters[varLoadData->var]));
    emit_mem_op(ctx, ScriptOp_MemStore, ctx->varRegisters[varLoadData->var], data->key);
    return ScriptCompileError_None;
  }

  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg(tgt.reg), data->val))) {
    return err;
  }
  emit_mem_op(ctx, ScriptOp_MemStore, tgt.reg, data->key);
  return err;
}

static ScriptCompileError compile_intr_zero(Context* ctx, const Target tgt, const ScriptOp op) {
  emit_unary(ctx, op, tgt.reg);
  return ScriptCompileError_None;
}

static ScriptCompileError
compile_intr_unary(Context* ctx, const Target tgt, const ScriptOp op, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg(tgt.reg), args[0]))) {
    return err;
  }
  emit_unary(ctx, op, tgt.reg);
  return err;
}

static ScriptCompileError
compile_intr_binary(Context* ctx, const Target tgt, const ScriptOp op, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg(tgt.reg), args[0]))) {
    return err;
  }
  const RegId tmpReg = reg_alloc(ctx);
  if (sentinel_check(tmpReg)) {
    err = ScriptCompileError_TooManyRegisters;
    return err;
  }
  if ((err = compile_expr(ctx, target_reg(tmpReg), args[1]))) {
    return err;
  }
  emit_binary(ctx, op, tgt.reg, tmpReg);
  reg_free(ctx, tmpReg);
  return err;
}

static ScriptCompileError
compile_intr_ternary(Context* ctx, const Target tgt, const ScriptOp op, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg(tgt.reg), args[0]))) {
    return err;
  }
  const RegId tmpReg1 = reg_alloc(ctx), tmpReg2 = reg_alloc(ctx);
  if (sentinel_check(tmpReg1) || sentinel_check(tmpReg2)) {
    err = ScriptCompileError_TooManyRegisters;
    return err;
  }
  if ((err = compile_expr(ctx, target_reg(tmpReg1), args[1]))) {
    return err;
  }
  if ((err = compile_expr(ctx, target_reg(tmpReg2), args[2]))) {
    return err;
  }
  emit_ternary(ctx, op, tgt.reg, tmpReg1, tmpReg2);
  reg_free(ctx, tmpReg1);
  reg_free(ctx, tmpReg2);
  return err;
}

static ScriptCompileError
compile_intr_quaternary(Context* ctx, const Target tgt, const ScriptOp op, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg(tgt.reg), args[0]))) {
    return err;
  }
  const RegId tmpReg1 = reg_alloc(ctx), tmpReg2 = reg_alloc(ctx), tmpReg3 = reg_alloc(ctx);
  if (sentinel_check(tmpReg1) || sentinel_check(tmpReg2) || sentinel_check(tmpReg3)) {
    err = ScriptCompileError_TooManyRegisters;
    return err;
  }
  if ((err = compile_expr(ctx, target_reg(tmpReg1), args[1]))) {
    return err;
  }
  if ((err = compile_expr(ctx, target_reg(tmpReg2), args[2]))) {
    return err;
  }
  if ((err = compile_expr(ctx, target_reg(tmpReg3), args[3]))) {
    return err;
  }
  emit_quaternary(ctx, op, tgt.reg, tmpReg1, tmpReg2, tmpReg3);
  reg_free(ctx, tmpReg1);
  reg_free(ctx, tmpReg2);
  reg_free(ctx, tmpReg3);
  return err;
}

static ScriptCompileError
compile_assert(Context* ctx, const Target tgt, const ScriptExpr expr, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg_cond(tgt.reg), args[0]))) {
    return err;
  }
  emit_location(ctx, expr);
  emit_unary(ctx, ScriptOp_Assert, tgt.reg);
  return err;
}

/**
 * Check if the inverse of the given expression would be cheaper to compute then the regular value.
 */
static bool compile_expr_prefer_invert(Context* ctx, const ScriptExpr e) {
  static const ScriptIntrinsic g_preferInvertIntrs[] = {
      ScriptIntrinsic_Invert,
      ScriptIntrinsic_NotEqual,
      ScriptIntrinsic_LessOrEqual,
      ScriptIntrinsic_GreaterOrEqual,
  };
  for (u32 i = 0; i != array_elems(g_preferInvertIntrs); ++i) {
    if (expr_is_intrinsic(ctx, e, g_preferInvertIntrs[i])) {
      return true;
    }
  }
  return false;
}

/**
 * Produce the inverted value of the given expression.
 * For various expressions this can be encoded more efficiently then just a naive invert at the end.
 */
static ScriptCompileError compile_expr_invert(Context* ctx, const Target tgt, const ScriptExpr e) {

  // Fast path: '!a' -> 'a'.
  if (tgt.condition && expr_is_intrinsic(ctx, e, ScriptIntrinsic_Invert)) {
    const ScriptExprIntrinsic* intrData = &expr_data(ctx->doc, e)->intrinsic;
    const ScriptExpr*          intrArgs = expr_set_data(ctx->doc, intrData->argSet);
    return compile_expr(ctx, tgt, intrArgs[0]);
  }

  static const struct {
    ScriptIntrinsic intr;
    ScriptOp        op;
  } g_inverseBinaryOps[] = {
      {ScriptIntrinsic_NotEqual, ScriptOp_Equal},      // 'a != b' -> 'a == b'.
      {ScriptIntrinsic_LessOrEqual, ScriptOp_Greater}, // 'a <= b' -> 'a > b'.
      {ScriptIntrinsic_GreaterOrEqual, ScriptOp_Less}, // 'a >= b' -> 'a < b'.
  };

  // Fast path: Inverse binary operations.
  for (u32 i = 0; i != array_elems(g_inverseBinaryOps); ++i) {
    if (expr_is_intrinsic(ctx, e, g_inverseBinaryOps[i].intr)) {
      const ScriptExprIntrinsic* intrData = &expr_data(ctx->doc, e)->intrinsic;
      const ScriptExpr*          intrArgs = expr_set_data(ctx->doc, intrData->argSet);
      return compile_intr_binary(ctx, tgt, g_inverseBinaryOps[i].op, intrArgs);
    }
  }

  // Generic path: 'a' -> '!a'.
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg_cond(tgt.reg), e))) {
    return err;
  }
  if (!tgt.optional) {
    emit_unary(ctx, ScriptOp_Invert, tgt.reg);
  }
  return err;
}

static ScriptCompileError
compile_intr_select(Context* ctx, const Target tgt, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  // Condition.
  const bool invert = compile_expr_prefer_invert(ctx, args[0]);
  if (invert) {
    if ((err = compile_expr_invert(ctx, target_reg_cond(tgt.reg), args[0]))) {
      return err;
    }
  } else {
    if ((err = compile_expr(ctx, target_reg_cond(tgt.reg), args[0]))) {
      return err;
    }
  }
  const LabelId retLabel = label_alloc(ctx), falseLabel = label_alloc(ctx);
  if (invert) {
    emit_jump_if_truthy(ctx, tgt.reg, falseLabel);
  } else {
    emit_jump_if_falsy(ctx, tgt.reg, falseLabel);
  }

  // If branch.
  if ((err = compile_expr(ctx, tgt, args[1]))) {
    return err;
  }
  const bool skipElse = tgt.optional && expr_is_null(ctx, args[2]);
  if (!skipElse) {
    emit_jump(ctx, retLabel); // Skip over the else branch.
  }

  label_link(ctx, falseLabel);

  // Else branch.
  if (!skipElse && (err = compile_expr(ctx, tgt, args[2]))) {
    return err;
  }

  label_link(ctx, retLabel);
  return err;
}

static ScriptCompileError
compile_intr_null_coalescing(Context* ctx, const Target tgt, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg(tgt.reg), args[0]))) {
    return err;
  }
  const LabelId retLabel = label_alloc(ctx);
  emit_jump_if_non_null(ctx, tgt.reg, retLabel);

  if ((err = compile_expr(ctx, target_reg(tgt.reg), args[1]))) {
    return err;
  }

  label_link(ctx, retLabel);
  return err;
}

static ScriptCompileError
compile_intr_logic_and(Context* ctx, const Target tgt, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg_cond(tgt.reg), args[0]))) {
    return err;
  }
  const LabelId retLabel = label_alloc(ctx);
  emit_jump_if_falsy(ctx, tgt.reg, retLabel);

  if ((err = compile_expr(ctx, target_reg_cond(tgt.reg), args[1]))) {
    return err;
  }

  label_link(ctx, retLabel);
  if (!tgt.condition && !tgt.optional) {
    emit_unary(ctx, ScriptOp_Truthy, tgt.reg); // Convert the result to boolean.
  }
  return err;
}

static ScriptCompileError
compile_intr_logic_or(Context* ctx, const Target tgt, const ScriptExpr* args) {
  ScriptCompileError err = ScriptCompileError_None;
  if ((err = compile_expr(ctx, target_reg_cond(tgt.reg), args[0]))) {
    return err;
  }
  const LabelId retLabel = label_alloc(ctx);
  emit_jump_if_truthy(ctx, tgt.reg, retLabel);

  if ((err = compile_expr(ctx, target_reg_cond(tgt.reg), args[1]))) {
    return err;
  }

  label_link(ctx, retLabel);
  if (!tgt.condition && !tgt.optional) {
    emit_unary(ctx, ScriptOp_Truthy, tgt.reg); // Convert the result to boolean.
  }
  return err;
}

static ScriptCompileError
compile_intr_loop(Context* ctx, const Target tgt, const ScriptExpr* args) {
  ScriptCompileError err    = ScriptCompileError_None;
  const RegId        tmpReg = reg_alloc(ctx);
  if (sentinel_check(tmpReg)) {
    err = ScriptCompileError_TooManyRegisters;
    return err;
  }

  // Initialize output to null in case the loop body is never entered.
  if (!tgt.optional && !expr_is_true(ctx, args[1])) {
    emit_unary(ctx, ScriptOp_ValueNull, tgt.reg);
  }

  // Setup expression.
  if (!expr_is_null(ctx, args[0])) {
    if ((err = compile_expr(ctx, target_reg_opt(tmpReg), args[0]))) {
      return err;
    }
  }
  const LabelId labelCond      = label_alloc(ctx);
  const LabelId labelIncrement = label_alloc(ctx);
  const LabelId labelEnd       = label_alloc(ctx);

  // Condition expression.
  if (expr_is_null(ctx, args[2])) {
    // NOTE: Loop is not using a increment expression; we can skip straight to the condition.
    label_link(ctx, labelIncrement);
  }
  label_link(ctx, labelCond);
  if (!expr_is_true(ctx, args[1])) {
    const bool invert = compile_expr_prefer_invert(ctx, args[1]);
    if (invert) {
      if ((err = compile_expr_invert(ctx, target_reg_cond(tmpReg), args[1]))) {
        return err;
      }
      emit_jump_if_truthy(ctx, tmpReg, labelEnd);
    } else {
      if ((err = compile_expr(ctx, target_reg_cond(tmpReg), args[1]))) {
        return err;
      }
      emit_jump_if_falsy(ctx, tmpReg, labelEnd);
    }
  }

  const LabelId prevLabelIncrement = ctx->loopLabelIncrement;
  const LabelId prevLabelEnd       = ctx->loopLabelEnd;

  ctx->loopLabelIncrement = labelIncrement;
  ctx->loopLabelEnd       = labelEnd;

  // Body expression.
  if ((err = compile_expr(ctx, tgt, args[3]))) {
    return err;
  }

  ctx->loopLabelIncrement = prevLabelIncrement;
  ctx->loopLabelEnd       = prevLabelEnd;

  // Increment expression.
  if (!expr_is_null(ctx, args[2])) {
    label_link(ctx, labelIncrement);
    if ((err = compile_expr(ctx, target_reg_opt(tmpReg), args[2]))) {
      return err;
    }
  }
  emit_jump(ctx, labelCond);

  label_link(ctx, labelEnd);

  reg_free(ctx, tmpReg);
  return err;
}

static ScriptCompileError compile_intr_continue(Context* ctx) {
  diag_assert(!sentinel_check(ctx->loopLabelIncrement));
  emit_jump(ctx, ctx->loopLabelIncrement);
  return ScriptCompileError_None;
}

static ScriptCompileError compile_intr_break(Context* ctx) {
  diag_assert(!sentinel_check(ctx->loopLabelEnd));
  emit_jump(ctx, ctx->loopLabelEnd);
  return ScriptCompileError_None;
}

static ScriptCompileError compile_intr(Context* ctx, const Target tgt, const ScriptExpr e) {
  ScriptCompileError         err  = ScriptCompileError_None;
  const ScriptExprIntrinsic* data = &expr_data(ctx->doc, e)->intrinsic;
  const ScriptExpr*          args = expr_set_data(ctx->doc, data->argSet);
  switch (data->intrinsic) {
  case ScriptIntrinsic_Continue:
    return compile_intr_continue(ctx);
  case ScriptIntrinsic_Break:
    return compile_intr_break(ctx);
  case ScriptIntrinsic_Return: {
    if (expr_is_null(ctx, args[0])) {
      emit_op(ctx, ScriptOp_ReturnNull);
      return ScriptCompileError_None;
    }
    return compile_intr_unary(ctx, tgt, ScriptOp_Return, args);
  }
  case ScriptIntrinsic_Type:
    return compile_intr_unary(ctx, tgt, ScriptOp_Type, args);
  case ScriptIntrinsic_Hash:
    return compile_intr_unary(ctx, tgt, ScriptOp_Hash, args);
  case ScriptIntrinsic_Assert:
    return compile_assert(ctx, tgt, e, args);
  case ScriptIntrinsic_MemLoadDynamic:
    return compile_intr_unary(ctx, tgt, ScriptOp_MemLoadDyn, args);
  case ScriptIntrinsic_MemStoreDynamic:
    return compile_intr_binary(ctx, tgt, ScriptOp_MemStoreDyn, args);
  case ScriptIntrinsic_Select:
    return compile_intr_select(ctx, tgt, args);
  case ScriptIntrinsic_NullCoalescing:
    return compile_intr_null_coalescing(ctx, tgt, args);
  case ScriptIntrinsic_LogicAnd:
    return compile_intr_logic_and(ctx, tgt, args);
  case ScriptIntrinsic_LogicOr:
    return compile_intr_logic_or(ctx, tgt, args);
  case ScriptIntrinsic_Loop:
    return compile_intr_loop(ctx, tgt, args);
  case ScriptIntrinsic_Equal:
    return compile_intr_binary(ctx, tgt, ScriptOp_Equal, args);
  case ScriptIntrinsic_NotEqual: {
    if (expr_is_null(ctx, args[0])) {
      return compile_intr_unary(ctx, tgt, ScriptOp_NonNull, &args[1]);
    }
    if (expr_is_null(ctx, args[1])) {
      return compile_intr_unary(ctx, tgt, ScriptOp_NonNull, &args[0]);
    }
    err = compile_intr_binary(ctx, target_reg_cond(tgt.reg), ScriptOp_Equal, args);
    if (!err && !tgt.optional) {
      emit_unary(ctx, ScriptOp_Invert, tgt.reg);
    }
    return err;
  }
  case ScriptIntrinsic_Less:
    return compile_intr_binary(ctx, tgt, ScriptOp_Less, args);
  case ScriptIntrinsic_LessOrEqual: {
    err = compile_intr_binary(ctx, target_reg_cond(tgt.reg), ScriptOp_Greater, args);
    if (!err && !tgt.optional) {
      emit_unary(ctx, ScriptOp_Invert, tgt.reg);
    }
    return err;
  }
  case ScriptIntrinsic_Greater:
    return compile_intr_binary(ctx, tgt, ScriptOp_Greater, args);
  case ScriptIntrinsic_GreaterOrEqual: {
    err = compile_intr_binary(ctx, target_reg_cond(tgt.reg), ScriptOp_Less, args);
    if (!err && !tgt.optional) {
      emit_unary(ctx, ScriptOp_Invert, tgt.reg);
    }
    return err;
  }
  case ScriptIntrinsic_Add:
    return compile_intr_binary(ctx, tgt, ScriptOp_Add, args);
  case ScriptIntrinsic_Sub:
    return compile_intr_binary(ctx, tgt, ScriptOp_Sub, args);
  case ScriptIntrinsic_Mul:
    return compile_intr_binary(ctx, tgt, ScriptOp_Mul, args);
  case ScriptIntrinsic_Div:
    return compile_intr_binary(ctx, tgt, ScriptOp_Div, args);
  case ScriptIntrinsic_Mod:
    return compile_intr_binary(ctx, tgt, ScriptOp_Mod, args);
  case ScriptIntrinsic_Negate:
    return compile_intr_unary(ctx, tgt, ScriptOp_Negate, args);
  case ScriptIntrinsic_Invert:
    return compile_expr_invert(ctx, tgt, args[0]);
  case ScriptIntrinsic_Distance:
    return compile_intr_binary(ctx, tgt, ScriptOp_Distance, args);
  case ScriptIntrinsic_Angle:
    return compile_intr_binary(ctx, tgt, ScriptOp_Angle, args);
  case ScriptIntrinsic_Sin:
    return compile_intr_unary(ctx, tgt, ScriptOp_Sin, args);
  case ScriptIntrinsic_Cos:
    return compile_intr_unary(ctx, tgt, ScriptOp_Cos, args);
  case ScriptIntrinsic_Normalize:
    return compile_intr_unary(ctx, tgt, ScriptOp_Normalize, args);
  case ScriptIntrinsic_Magnitude:
    return compile_intr_unary(ctx, tgt, ScriptOp_Magnitude, args);
  case ScriptIntrinsic_Absolute:
    return compile_intr_unary(ctx, tgt, ScriptOp_Absolute, args);
  case ScriptIntrinsic_VecX:
    return compile_intr_unary(ctx, tgt, ScriptOp_VecX, args);
  case ScriptIntrinsic_VecY:
    return compile_intr_unary(ctx, tgt, ScriptOp_VecY, args);
  case ScriptIntrinsic_VecZ:
    return compile_intr_unary(ctx, tgt, ScriptOp_VecZ, args);
  case ScriptIntrinsic_Vec3Compose:
    return compile_intr_ternary(ctx, tgt, ScriptOp_Vec3Compose, args);
  case ScriptIntrinsic_QuatFromEuler:
    return compile_intr_ternary(ctx, tgt, ScriptOp_QuatFromEuler, args);
  case ScriptIntrinsic_QuatFromAngleAxis:
    return compile_intr_binary(ctx, tgt, ScriptOp_QuatFromAngleAxis, args);
  case ScriptIntrinsic_ColorR:
    return compile_intr_unary(ctx, tgt, ScriptOp_ColorR, args);
  case ScriptIntrinsic_ColorG:
    return compile_intr_unary(ctx, tgt, ScriptOp_ColorG, args);
  case ScriptIntrinsic_ColorB:
    return compile_intr_unary(ctx, tgt, ScriptOp_ColorB, args);
  case ScriptIntrinsic_ColorA:
    return compile_intr_unary(ctx, tgt, ScriptOp_ColorA, args);
  case ScriptIntrinsic_ColorCompose:
    return compile_intr_quaternary(ctx, tgt, ScriptOp_ColorCompose, args);
  case ScriptIntrinsic_ColorComposeHsv:
    return compile_intr_quaternary(ctx, tgt, ScriptOp_ColorComposeHsv, args);
  case ScriptIntrinsic_ColorFor:
    return compile_intr_unary(ctx, tgt, ScriptOp_ColorFor, args);
  case ScriptIntrinsic_Random:
    return compile_intr_zero(ctx, tgt, ScriptOp_Random);
  case ScriptIntrinsic_RandomSphere:
    return compile_intr_zero(ctx, tgt, ScriptOp_RandomSphere);
  case ScriptIntrinsic_RandomCircleXZ:
    return compile_intr_zero(ctx, tgt, ScriptOp_RandomCircleXZ);
  case ScriptIntrinsic_RandomBetween:
    return compile_intr_binary(ctx, tgt, ScriptOp_RandomBetween, args);
  case ScriptIntrinsic_RoundDown:
    return compile_intr_unary(ctx, tgt, ScriptOp_RoundDown, args);
  case ScriptIntrinsic_RoundNearest:
    return compile_intr_unary(ctx, tgt, ScriptOp_RoundNearest, args);
  case ScriptIntrinsic_RoundUp:
    return compile_intr_unary(ctx, tgt, ScriptOp_RoundUp, args);
  case ScriptIntrinsic_Clamp:
    return compile_intr_ternary(ctx, tgt, ScriptOp_Clamp, args);
  case ScriptIntrinsic_Lerp:
    return compile_intr_ternary(ctx, tgt, ScriptOp_Lerp, args);
  case ScriptIntrinsic_Min:
    return compile_intr_binary(ctx, tgt, ScriptOp_Min, args);
  case ScriptIntrinsic_Max:
    return compile_intr_binary(ctx, tgt, ScriptOp_Max, args);
  case ScriptIntrinsic_Perlin3:
    return compile_intr_unary(ctx, tgt, ScriptOp_Perlin3, args);
  case ScriptIntrinsic_Count:
    break;
  }
  diag_assert_fail("Unknown intrinsic");
  UNREACHABLE
}

static ScriptCompileError compile_block(Context* ctx, const Target tgt, const ScriptExpr e) {
  const ScriptExprBlock* data  = &expr_data(ctx->doc, e)->block;
  const ScriptExpr*      exprs = expr_set_data(ctx->doc, data->exprSet);

  // NOTE: Blocks need at least one expression.
  ScriptCompileError err = ScriptCompileError_None;
  for (u32 i = 0; i != data->exprCount; ++i) {
    const bool last = i == (data->exprCount - 1);
    // For all but the last expression the output is optional (as it will never be observed).
    if ((err = compile_expr(ctx, last ? tgt : target_reg_opt(tgt.reg), exprs[i]))) {
      break;
    }
  }
  return err;
}

static ScriptCompileError compile_extern(Context* ctx, const Target tgt, const ScriptExpr e) {
  const ScriptExprExtern* data     = &expr_data(ctx->doc, e)->extern_;
  const ScriptExpr*       argExprs = expr_set_data(ctx->doc, data->argSet);
  if (data->argCount == 1 && expr_is_var_load(ctx, argExprs[0])) {
    /**
     * Fast path for calling an extern func with a single variable argument, in this case we can
     * skip loading the variable and pass the variable register directly as an argument set.
     */
    const ScriptExprVarLoad* varLoadData = &expr_data(ctx->doc, argExprs[0])->var_load;
    diag_assert(!sentinel_check(ctx->varRegisters[varLoadData->var]));
    const RegSet argRegs = {.begin = ctx->varRegisters[varLoadData->var], .count = 1};
    emit_extern(ctx, tgt.reg, data->func, argRegs);
    return ScriptCompileError_None;
  }
  const RegSet argRegs = reg_alloc_set(ctx, data->argCount);
  if (sentinel_check(argRegs.begin)) {
    return ScriptCompileError_TooManyRegisters;
  }
  ScriptCompileError err = ScriptCompileError_None;
  for (u32 i = 0; i != data->argCount; ++i) {
    if ((err = compile_expr(ctx, target_reg(argRegs.begin + i), argExprs[i]))) {
      goto Ret;
    }
  }
  emit_location(ctx, e);
  emit_extern(ctx, tgt.reg, data->func, argRegs);
  reg_free_set(ctx, argRegs);
Ret:
  return err;
}

static ScriptCompileError compile_expr(Context* ctx, const Target tgt, const ScriptExpr e) {
  switch (expr_kind(ctx->doc, e)) {
  case ScriptExprKind_Value:
    return compile_value(ctx, tgt, e);
  case ScriptExprKind_VarLoad:
    return compile_var_load(ctx, tgt, e);
  case ScriptExprKind_VarStore:
    return compile_var_store(ctx, tgt, e);
  case ScriptExprKind_MemLoad:
    return compile_mem_load(ctx, tgt, e);
  case ScriptExprKind_MemStore:
    return compile_mem_store(ctx, tgt, e);
  case ScriptExprKind_Intrinsic:
    return compile_intr(ctx, tgt, e);
  case ScriptExprKind_Block:
    return compile_block(ctx, tgt, e);
  case ScriptExprKind_Extern:
    return compile_extern(ctx, tgt, e);
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

ScriptCompileError script_compile(
    const ScriptDoc*    doc,
    const ScriptLookup* lookup,
    const ScriptExpr    expr,
    Allocator*          outAlloc,
    ScriptProgram*      out) {
  Context ctx = {
      .doc                = doc,
      .lookup             = lookup,
      .outCode            = dynstring_create(g_allocHeap, 64),
      .outLiterals        = dynarray_create_t(g_allocHeap, ScriptVal, 0),
      .outLocations       = dynarray_create_t(g_allocHeap, ScriptProgramLoc, 16),
      .labels             = dynarray_create_t(g_allocHeap, Label, 0),
      .labelPatches       = dynarray_create_t(g_allocHeap, LabelPatch, 0),
      .loopLabelIncrement = sentinel_u32,
      .loopLabelEnd       = sentinel_u32,
  };
  mem_set(array_mem(ctx.varRegisters), 0xFF);

  reg_free_all(&ctx);
  diag_assert(reg_available(&ctx) == script_prog_regs);

  const RegId resultReg = reg_alloc(&ctx);
  diag_assert(resultReg < script_prog_regs);

  ScriptCompileError err = compile_expr(&ctx, target_reg(resultReg), expr);
  if (UNLIKELY(err)) {
    goto Ret;
  }

  if (ctx.lastOp != ScriptOp_Return && ctx.lastOp != ScriptOp_ReturnNull) {
    emit_unary(&ctx, ScriptOp_Return, resultReg);
  }

  // Verify that output limit was not exceeded.
  if (UNLIKELY(ctx.outCode.size > u16_max)) {
    err = ScriptCompileError_CodeLimitExceeded;
  }

  // Verify no registers where leaked.
  reg_free(&ctx, resultReg);
  array_for_t(ctx.varRegisters, RegId, varReg) {
    if (!sentinel_check(*varReg)) {
      reg_free(&ctx, *varReg);
    }
  }
  diag_assert_msg(reg_available(&ctx) == script_prog_regs, "Not all registers freed");

  // Create program.
  dynarray_sort(&ctx.outLocations, script_compare_loc);
  *out = (ScriptProgram){
      .code.ptr         = string_dup(outAlloc, dynstring_view(&ctx.outCode)).ptr,
      .code.size        = ctx.outCode.size,
      .binderHash       = doc->binderHash,
      .literals.values  = dynarray_copy_as_new(&ctx.outLiterals, outAlloc),
      .literals.count   = ctx.outLiterals.size,
      .locations.values = dynarray_copy_as_new(&ctx.outLocations, outAlloc),
      .locations.count  = ctx.outLocations.size,
  };

Ret:
  dynstring_destroy(&ctx.outCode);
  dynarray_destroy(&ctx.outLiterals);
  dynarray_destroy(&ctx.outLocations);
  dynarray_destroy(&ctx.labels);
  dynarray_destroy(&ctx.labelPatches);
  return err;
}
