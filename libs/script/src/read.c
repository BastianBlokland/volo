#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_utf8.h"
#include "script_binder.h"
#include "script_diag.h"
#include "script_lex.h"
#include "script_read.h"
#include "script_sym.h"

#include "doc_internal.h"

#define script_depth_max 25
#define script_block_size_max 128
#define script_args_max 10
#define script_builtin_consts_max 32
#define script_builtin_funcs_max 32
#define script_tracked_mem_keys_max 32

typedef struct {
  StringHash idHash;
  ScriptVal  val;
  String     id;
} ScriptBuiltinConst;

static ScriptBuiltinConst g_scriptBuiltinConsts[script_builtin_consts_max];
static u32                g_scriptBuiltinConstCount;

static void script_builtin_const_add(const String id, const ScriptVal val) {
  diag_assert(g_scriptBuiltinConstCount != script_builtin_consts_max);
  g_scriptBuiltinConsts[g_scriptBuiltinConstCount++] = (ScriptBuiltinConst){
      .id     = id,
      .idHash = string_hash(id),
      .val    = val,
  };
}

static const ScriptBuiltinConst* script_builtin_const_lookup(const StringHash id) {
  for (u32 i = 0; i != g_scriptBuiltinConstCount; ++i) {
    if (g_scriptBuiltinConsts[i].idHash == id) {
      return &g_scriptBuiltinConsts[i];
    }
  }
  return null;
}

typedef struct {
  StringHash      idHash;
  u32             argCount;
  ScriptIntrinsic intr;
  String          id;
} ScriptBuiltinFunc;

static ScriptBuiltinFunc g_scriptBuiltinFuncs[script_builtin_funcs_max];
static u32               g_scriptBuiltinFuncCount;

static void script_builtin_func_add(const String id, const ScriptIntrinsic intr) {
  diag_assert(g_scriptBuiltinFuncCount != script_builtin_funcs_max);
  g_scriptBuiltinFuncs[g_scriptBuiltinFuncCount++] = (ScriptBuiltinFunc){
      .idHash   = string_hash(id),
      .argCount = script_intrinsic_arg_count(intr),
      .intr     = intr,
      .id       = id,
  };
}

static bool script_builtin_func_exists(const StringHash id) {
  for (u32 i = 0; i != g_scriptBuiltinFuncCount; ++i) {
    if (g_scriptBuiltinFuncs[i].idHash == id) {
      return true;
    }
  }
  return false;
}

static const ScriptBuiltinFunc* script_builtin_func_lookup(const StringHash id, const u32 argc) {
  for (u32 i = 0; i != g_scriptBuiltinFuncCount; ++i) {
    if (g_scriptBuiltinFuncs[i].idHash == id && g_scriptBuiltinFuncs[i].argCount == argc) {
      return &g_scriptBuiltinFuncs[i];
    }
  }
  return null;
}

static void script_builtin_init() {
  diag_assert(g_scriptBuiltinConstCount == 0);
  diag_assert(g_scriptBuiltinFuncCount == 0);

  // Builtin constants.
  script_builtin_const_add(string_lit("null"), script_null());
  script_builtin_const_add(string_lit("true"), script_bool(true));
  script_builtin_const_add(string_lit("false"), script_bool(false));
  script_builtin_const_add(string_lit("pi"), script_number(math_pi_f64));
  script_builtin_const_add(string_lit("deg_to_rad"), script_number(math_deg_to_rad));
  script_builtin_const_add(string_lit("rad_to_deg"), script_number(math_rad_to_deg));
  script_builtin_const_add(string_lit("up"), script_vector3(geo_up));
  script_builtin_const_add(string_lit("down"), script_vector3(geo_down));
  script_builtin_const_add(string_lit("left"), script_vector3(geo_left));
  script_builtin_const_add(string_lit("right"), script_vector3(geo_right));
  script_builtin_const_add(string_lit("forward"), script_vector3(geo_forward));
  script_builtin_const_add(string_lit("backward"), script_vector3(geo_backward));
  script_builtin_const_add(string_lit("quat_ident"), script_quat(geo_quat_ident));

  // Builtin functions.
  script_builtin_func_add(string_lit("type"), ScriptIntrinsic_Type);
  script_builtin_func_add(string_lit("vector"), ScriptIntrinsic_Vector3Compose);
  script_builtin_func_add(string_lit("vector_x"), ScriptIntrinsic_VectorX);
  script_builtin_func_add(string_lit("vector_y"), ScriptIntrinsic_VectorY);
  script_builtin_func_add(string_lit("vector_z"), ScriptIntrinsic_VectorZ);
  script_builtin_func_add(string_lit("euler"), ScriptIntrinsic_QuatFromEuler);
  script_builtin_func_add(string_lit("angle_axis"), ScriptIntrinsic_QuatFromAngleAxis);
  script_builtin_func_add(string_lit("distance"), ScriptIntrinsic_Distance);
  script_builtin_func_add(string_lit("distance"), ScriptIntrinsic_Magnitude);
  script_builtin_func_add(string_lit("normalize"), ScriptIntrinsic_Normalize);
  script_builtin_func_add(string_lit("angle"), ScriptIntrinsic_Angle);
  script_builtin_func_add(string_lit("random"), ScriptIntrinsic_Random);
  script_builtin_func_add(string_lit("random"), ScriptIntrinsic_RandomBetween);
  script_builtin_func_add(string_lit("random_sphere"), ScriptIntrinsic_RandomSphere);
  script_builtin_func_add(string_lit("random_circle_xz"), ScriptIntrinsic_RandomCircleXZ);
  script_builtin_func_add(string_lit("round_down"), ScriptIntrinsic_RoundDown);
  script_builtin_func_add(string_lit("round_nearest"), ScriptIntrinsic_RoundNearest);
  script_builtin_func_add(string_lit("round_up"), ScriptIntrinsic_RoundUp);
  script_builtin_func_add(string_lit("assert"), ScriptIntrinsic_Assert);
}

typedef enum {
  OpPrecedence_None,
  OpPrecedence_Assignment,
  OpPrecedence_Conditional,
  OpPrecedence_Logical,
  OpPrecedence_Equality,
  OpPrecedence_Relational,
  OpPrecedence_Additive,
  OpPrecedence_Multiplicative,
  OpPrecedence_Unary,
} OpPrecedence;

static OpPrecedence op_precedence(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_EqEq:
  case ScriptTokenType_BangEq:
    return OpPrecedence_Equality;
  case ScriptTokenType_Le:
  case ScriptTokenType_LeEq:
  case ScriptTokenType_Gt:
  case ScriptTokenType_GtEq:
    return OpPrecedence_Relational;
  case ScriptTokenType_Plus:
  case ScriptTokenType_Minus:
    return OpPrecedence_Additive;
  case ScriptTokenType_Star:
  case ScriptTokenType_Slash:
  case ScriptTokenType_Percent:
    return OpPrecedence_Multiplicative;
  case ScriptTokenType_AmpAmp:
  case ScriptTokenType_PipePipe:
    return OpPrecedence_Logical;
  case ScriptTokenType_QMark:
  case ScriptTokenType_QMarkQMark:
    return OpPrecedence_Conditional;
  default:
    return OpPrecedence_None;
  }
}

static ScriptIntrinsic token_op_unary(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_Minus:
    return ScriptIntrinsic_Negate;
  case ScriptTokenType_Bang:
    return ScriptIntrinsic_Invert;
  default:
    diag_assert_fail("Invalid unary operation token");
    UNREACHABLE
  }
}

static ScriptIntrinsic token_op_binary(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_EqEq:
    return ScriptIntrinsic_Equal;
  case ScriptTokenType_BangEq:
    return ScriptIntrinsic_NotEqual;
  case ScriptTokenType_Le:
    return ScriptIntrinsic_Less;
  case ScriptTokenType_LeEq:
    return ScriptIntrinsic_LessOrEqual;
  case ScriptTokenType_Gt:
    return ScriptIntrinsic_Greater;
  case ScriptTokenType_GtEq:
    return ScriptIntrinsic_GreaterOrEqual;
  case ScriptTokenType_Plus:
    return ScriptIntrinsic_Add;
  case ScriptTokenType_Minus:
    return ScriptIntrinsic_Sub;
  case ScriptTokenType_Star:
    return ScriptIntrinsic_Mul;
  case ScriptTokenType_Slash:
    return ScriptIntrinsic_Div;
  case ScriptTokenType_Percent:
    return ScriptIntrinsic_Mod;
  case ScriptTokenType_AmpAmp:
    return ScriptIntrinsic_LogicAnd;
  case ScriptTokenType_PipePipe:
    return ScriptIntrinsic_LogicOr;
  case ScriptTokenType_QMarkQMark:
    return ScriptIntrinsic_NullCoalescing;
  default:
    diag_assert_fail("Invalid binary operation token");
    UNREACHABLE
  }
}

static ScriptIntrinsic token_op_binary_modify(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_PlusEq:
    return ScriptIntrinsic_Add;
  case ScriptTokenType_MinusEq:
    return ScriptIntrinsic_Sub;
  case ScriptTokenType_StarEq:
    return ScriptIntrinsic_Mul;
  case ScriptTokenType_SlashEq:
    return ScriptIntrinsic_Div;
  case ScriptTokenType_PercentEq:
    return ScriptIntrinsic_Mod;
  case ScriptTokenType_QMarkQMarkEq:
    return ScriptIntrinsic_NullCoalescing;
  default:
    diag_assert_fail("Invalid binary modify operation token");
    UNREACHABLE
  }
}

static bool token_intr_rhs_scope(const ScriptIntrinsic intr) {
  switch (intr) {
  case ScriptIntrinsic_LogicAnd:
  case ScriptIntrinsic_LogicOr:
  case ScriptIntrinsic_NullCoalescing:
    return true;
  default:
    return false;
  }
}

typedef struct {
  StringHash  id;
  ScriptVarId varSlot;
  bool        used;
  ScriptRange declRange;       // NOTE: Not yet trimmed.
  ScriptPos   validUsageStart; // NOTE: Not yet trimmed.
} ScriptVarMeta;

typedef struct sScriptScope {
  ScriptVarMeta        vars[script_var_count];
  struct sScriptScope* next;
} ScriptScope;

typedef enum {
  ScriptReadFlags_ProgramInvalid = 1 << 0,
} ScriptReadFlags;

// clang-format off

typedef enum {
  ScriptSection_InsideLoop           = 1 << 0,
  ScriptSection_DisallowVarDeclare   = 1 << 1,
  ScriptSection_DisallowLoop         = 1 << 2,
  ScriptSection_DisallowIf           = 1 << 3,
  ScriptSection_DisallowReturn       = 1 << 4,
  ScriptSection_DisallowStatement    = 0 | ScriptSection_DisallowVarDeclare
                                         | ScriptSection_DisallowLoop
                                         | ScriptSection_DisallowIf
                                         | ScriptSection_DisallowReturn
                                       ,
  ScriptSection_ResetOnExplicitScope = 0 | ScriptSection_DisallowStatement
                                       ,
} ScriptSection;

// clang-format on

typedef struct {
  ScriptDoc*          doc;
  const ScriptBinder* binder;
  ScriptDiagBag*      diags;
  ScriptSymBag*       syms;
  String              input, inputTotal;
  ScriptScope*        scopeRoot;
  ScriptReadFlags     flags : 8;
  ScriptSection       section : 8;
  u16                 recursionDepth;
  u8                  varAvailability[bits_to_bytes(script_var_count) + 1]; // Bitmask of free vars.
  StringHash          trackedMemKeys[script_tracked_mem_keys_max];
} ScriptReadContext;

static ScriptSection read_section_add(ScriptReadContext* ctx, const ScriptSection flags) {
  const ScriptSection oldSection = ctx->section;
  ctx->section |= flags;
  return oldSection;
}

static ScriptSection read_section_reset(ScriptReadContext* ctx, const ScriptSection flags) {
  const ScriptSection oldSection = ctx->section;
  ctx->section &= ~flags;
  return oldSection;
}

static ScriptPos read_pos_current(ScriptReadContext* ctx) {
  return (ScriptPos)(ctx->inputTotal.size - ctx->input.size);
}

static ScriptRange read_range_current(ScriptReadContext* ctx, const ScriptPos start) {
  return script_range(start, read_pos_current(ctx));
}

static ScriptRange read_range_full(ScriptReadContext* ctx) {
  return script_range_full(ctx->inputTotal);
}

static ScriptRange read_range_trim(ScriptReadContext* ctx, const ScriptRange range) {
  return script_range(script_pos_trim(ctx->inputTotal, range.start), range.end);
}

static void
read_emit_err_range(ScriptReadContext* ctx, const ScriptError err, const ScriptRange range) {
  diag_assert(err != ScriptError_None);
  if (ctx->diags) {
    const ScriptDiag diag = {
        .type  = ScriptDiagType_Error,
        .error = err,
        .range = read_range_trim(ctx, range),
    };
    script_diag_push(ctx->diags, &diag);
  }
}

static void read_emit_err(ScriptReadContext* ctx, const ScriptError err, const ScriptPos start) {
  read_emit_err_range(ctx, err, read_range_current(ctx, start));
}

static void read_emit_unused_vars(ScriptReadContext* ctx, const ScriptScope* scope) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagType_Warning)) {
    return;
  }
  for (u32 i = 0; i != script_var_count; ++i) {
    if (!scope->vars[i].id) {
      break;
    }
    if (!scope->vars[i].used) {
      const ScriptDiag unusedDiag = {
          .type  = ScriptDiagType_Warning,
          .error = ScriptError_VarUnused,
          .range = read_range_trim(ctx, scope->vars[i].declRange),
      };
      script_diag_push(ctx->diags, &unusedDiag);
    }
  }
}

static void read_sym_push_vars(ScriptReadContext* ctx, const ScriptScope* scope) {
  if (!ctx->syms) {
    return;
  }
  for (u32 i = 0; i != script_var_count; ++i) {
    if (!scope->vars[i].id) {
      break;
    }
    const ScriptRange declRangeTrimmed = read_range_trim(ctx, scope->vars[i].declRange);

    const ScriptSym sym = {
        .type       = ScriptSymType_Variable,
        .label      = script_range_text(ctx->inputTotal, declRangeTrimmed),
        .validRange = read_range_current(ctx, scope->vars[i].validUsageStart),
    };
    script_sym_push(ctx->syms, &sym);
  }
}

static bool read_var_alloc(ScriptReadContext* ctx, ScriptVarId* out) {
  const usize index = bitset_next(mem_var(ctx->varAvailability), 0);
  if (UNLIKELY(sentinel_check(index))) {
    return false;
  }
  bitset_clear(mem_var(ctx->varAvailability), index);
  *out = (ScriptVarId)index;
  return true;
}

static void read_var_free(ScriptReadContext* ctx, const ScriptVarId var) {
  diag_assert(!bitset_test(mem_var(ctx->varAvailability), var));
  bitset_set(mem_var(ctx->varAvailability), var);
}

static void read_var_free_all(ScriptReadContext* ctx) {
  bitset_set_all(mem_var(ctx->varAvailability), script_var_count);
}

static ScriptScope* read_scope_head(ScriptReadContext* ctx) { return ctx->scopeRoot; }
static ScriptScope* read_scope_tail(ScriptReadContext* ctx) {
  ScriptScope* scope = ctx->scopeRoot;
  for (; scope->next; scope = scope->next)
    ;
  return scope;
}

static void read_scope_push(ScriptReadContext* ctx, ScriptScope* scope) {
  read_scope_tail(ctx)->next = scope;
}

static void read_scope_pop(ScriptReadContext* ctx) {
  ScriptScope* scope = read_scope_tail(ctx);
  diag_assert(scope && scope != ctx->scopeRoot);

  // Remove the scope from the scope list.
  ScriptScope* newTail = ctx->scopeRoot;
  for (; newTail->next != scope; newTail = newTail->next)
    ;
  newTail->next = null;

  read_sym_push_vars(ctx, scope);
  read_emit_unused_vars(ctx, scope);

  // Free all the variables that the scope declared.
  for (u32 i = 0; i != script_var_count; ++i) {
    if (scope->vars[i].id) {
      read_var_free(ctx, scope->vars[i].varSlot);
    }
  }
}

static bool read_var_declare(
    ScriptReadContext* ctx, const StringHash id, const ScriptRange declRange, ScriptVarId* out) {
  ScriptScope* scope = read_scope_tail(ctx);
  diag_assert(scope);

  for (u32 i = 0; i != script_var_count; ++i) {
    if (scope->vars[i].id) {
      continue; // Var already in use.
    }
    if (!read_var_alloc(ctx, out)) {
      return false;
    }
    scope->vars[i] = (ScriptVarMeta){
        .id              = id,
        .varSlot         = *out,
        .declRange       = declRange,
        .validUsageStart = read_pos_current(ctx), // TODO: Should this be an input parameter?
    };
    return true;
  }
  return false;
}

static ScriptVarMeta* read_var_lookup(ScriptReadContext* ctx, const StringHash id) {
  for (ScriptScope* scope = read_scope_head(ctx); scope; scope = scope->next) {
    for (u32 i = 0; i != script_var_count; ++i) {
      if (scope->vars[i].id == id) {
        return &scope->vars[i];
      }
      if (!scope->vars[i].id) {
        break;
      }
    }
  }
  return null;
}

static bool read_track_mem_key(ScriptReadContext* ctx, const StringHash key) {
  for (u32 i = 0; i != script_tracked_mem_keys_max; ++i) {
    if (ctx->trackedMemKeys[i] == key) {
      return true;
    }
    if (!ctx->trackedMemKeys[i]) {
      ctx->trackedMemKeys[i] = key;
      return true;
    }
  }
  return false;
}

static ScriptToken read_peek(ScriptReadContext* ctx) {
  ScriptToken token;
  script_lex(ctx->input, null, &token, ScriptLexFlags_None);
  return token;
}

static ScriptToken read_consume(ScriptReadContext* ctx) {
  ScriptToken token;
  ctx->input = script_lex(ctx->input, g_stringtable, &token, ScriptLexFlags_None);
  return token;
}

static bool read_consume_if(ScriptReadContext* ctx, const ScriptTokenType type) {
  ScriptToken  token;
  const String rem = script_lex(ctx->input, g_stringtable, &token, ScriptLexFlags_None);
  if (token.type == type) {
    ctx->input = rem;
    return true;
  }
  return false;
}

/**
 * We differentiate between two different kinds of failure while parsing:
 *
 * 1) Structural failure (for example a missing expression):
 *      At this point we do not know how to interpret the following tokens and thus we produce an
 *      invalid token (and generally stop parsing at that point).
 *
 * 2) Semantic failure (for example an unknown variable identifier):
 *      An dummy token is returned (null) and parsing continues.
 *
 * In both cases however we mark that the program is invalid and wont produce a valid output.
 */

static ScriptExpr read_fail_structural(ScriptReadContext* ctx) {
  ctx->flags |= ScriptReadFlags_ProgramInvalid;
  return script_expr_sentinel;
}

static ScriptExpr read_fail_semantic(ScriptReadContext* ctx) {
  ctx->flags |= ScriptReadFlags_ProgramInvalid;
  return script_add_value_anon(ctx->doc, script_null());
}

static ScriptExpr read_expr(ScriptReadContext*, OpPrecedence minPrecedence);

static void read_emit_unnecessary_semicolon(ScriptReadContext* ctx, const ScriptRange sepRange) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagType_Warning)) {
    return;
  }
  ScriptToken nextToken;
  script_lex(ctx->input, null, &nextToken, ScriptLexFlags_IncludeNewlines);
  if (UNLIKELY(nextToken.type == ScriptTokenType_Newline)) {
    const ScriptDiag unnecessaryDiag = {
        .type  = ScriptDiagType_Warning,
        .error = ScriptError_UnnecessarySemicolon,
        .range = read_range_trim(ctx, sepRange),
    };
    script_diag_push(ctx->diags, &unnecessaryDiag);
  }
}

static void read_visitor_has_side_effect(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  bool* hasSideEffect = ctx;
  switch (script_expr_type(doc, expr)) {
  case ScriptExprType_MemStore:
  case ScriptExprType_VarStore:
  case ScriptExprType_Extern:
    *hasSideEffect = true;
    return;
  case ScriptExprType_Value:
  case ScriptExprType_VarLoad:
  case ScriptExprType_MemLoad:
  case ScriptExprType_Block:
    return;
  case ScriptExprType_Intrinsic: {
    const ScriptExprData* data = dynarray_at_t(&doc->exprData, expr, ScriptExprData);
    switch (data->data_intrinsic.intrinsic) {
    case ScriptIntrinsic_Continue:
    case ScriptIntrinsic_Break:
    case ScriptIntrinsic_Return:
    case ScriptIntrinsic_Assert:
      *hasSideEffect = true;
      // Fallthrough.
    default:
      return;
    }
  }
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Unknown expression type");
  UNREACHABLE
}

static void read_emit_no_effect(
    ScriptReadContext* ctx,
    const ScriptExpr   exprs[],
    const ScriptRange  exprRanges[],
    const u32          exprCount) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagType_Warning)) {
    return;
  }
  for (u32 i = 0; i != (exprCount - 1); ++i) {
    bool hasSideEffect = false;
    script_expr_visit(ctx->doc, exprs[i], &hasSideEffect, read_visitor_has_side_effect);
    if (!hasSideEffect) {
      const ScriptDiag noEffectDiag = {
          .type  = ScriptDiagType_Warning,
          .error = ScriptError_ExprHasNoEffect,
          .range = read_range_trim(ctx, exprRanges[i]),
      };
      script_diag_push(ctx->diags, &noEffectDiag);
    }
  }
}

static void read_emit_unreachable(
    ScriptReadContext* ctx,
    const ScriptExpr   exprs[],
    const ScriptRange  exprRanges[],
    const u32          exprCount) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagType_Warning)) {
    return;
  }
  for (u32 i = 0; i != (exprCount - 1); ++i) {
    const ScriptDocSignal uncaughtSignal = script_expr_always_uncaught_signal(ctx->doc, exprs[i]);
    if (uncaughtSignal) {
      const ScriptPos  unreachableStart = exprRanges[i + 1].start;
      const ScriptPos  unreachableEnd   = exprRanges[exprCount - 1].end;
      const ScriptDiag unreachableDiag  = {
           .type  = ScriptDiagType_Warning,
           .error = ScriptError_ExprUnreachable,
           .range = read_range_trim(ctx, script_range(unreachableStart, unreachableEnd)),
      };
      script_diag_push(ctx->diags, &unreachableDiag);
      break;
    }
  }
}

typedef enum {
  ScriptBlockType_Implicit,
  ScriptBlockType_Explicit,
} ScriptBlockType;

static bool read_is_block_end(const ScriptTokenType tokenType, const ScriptBlockType blockType) {
  if (blockType == ScriptBlockType_Explicit && tokenType == ScriptTokenType_CurlyClose) {
    return true;
  }
  return tokenType == ScriptTokenType_End;
}

static bool read_is_block_separator(const ScriptTokenType tokenType) {
  return tokenType == ScriptTokenType_Newline || tokenType == ScriptTokenType_Semicolon;
}

static ScriptExpr read_expr_block(ScriptReadContext* ctx, const ScriptBlockType blockType) {
  const ScriptPos blockStart = read_pos_current(ctx);

  ScriptExpr  exprs[script_block_size_max];
  ScriptRange exprRanges[script_block_size_max];
  u32         exprCount = 0;

  if (read_is_block_end(read_peek(ctx).type, blockType)) {
    goto BlockEnd; // Empty block.
  }

BlockNext:
  if (UNLIKELY(exprCount == script_block_size_max)) {
    return read_emit_err(ctx, ScriptError_BlockTooBig, blockStart), read_fail_structural(ctx);
  }
  const ScriptPos  exprStart = read_pos_current(ctx);
  const ScriptExpr exprNew   = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(sentinel_check(exprNew))) {
    return read_fail_structural(ctx);
  }
  const ScriptRange exprRange = read_range_current(ctx, exprStart);
  exprs[exprCount]            = exprNew;
  exprRanges[exprCount]       = exprRange;
  ++exprCount;

  if (read_is_block_end(read_peek(ctx).type, blockType)) {
    goto BlockEnd;
  }

  const ScriptPos sepStart = read_pos_current(ctx);
  ScriptToken     sepToken;
  ctx->input = script_lex(ctx->input, g_stringtable, &sepToken, ScriptLexFlags_IncludeNewlines);
  const ScriptRange sepRange = read_range_current(ctx, sepStart);

  if (!read_is_block_separator(sepToken.type)) {
    read_emit_err_range(ctx, ScriptError_MissingSemicolon, exprRange);
    return read_fail_structural(ctx);
  }
  if (sepToken.type == ScriptTokenType_Semicolon) {
    read_emit_unnecessary_semicolon(ctx, sepRange);
  }
  if (!read_is_block_end(read_peek(ctx).type, blockType)) {
    goto BlockNext;
  }

BlockEnd:
  switch (exprCount) {
  case 0:
    return script_add_value_anon(ctx->doc, script_null());
  case 1:
    return exprs[0];
  default:
    read_emit_no_effect(ctx, exprs, exprRanges, exprCount);
    read_emit_unreachable(ctx, exprs, exprRanges, exprCount);
    const ScriptRange blockRange = read_range_current(ctx, blockStart);
    return script_add_block(ctx->doc, blockRange, exprs, exprCount);
  }
}

/**
 * NOTE: Caller is expected to consume the opening curly-brace.
 */
static ScriptExpr read_expr_scope_block(ScriptReadContext* ctx) {
  const ScriptPos start = read_pos_current(ctx);

  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  const ScriptSection prevSection = read_section_reset(ctx, ScriptSection_ResetOnExplicitScope);
  const ScriptExpr    expr        = read_expr_block(ctx, ScriptBlockType_Explicit);
  ctx->section                    = prevSection;

  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  if (UNLIKELY(sentinel_check(expr))) {
    return read_fail_structural(ctx);
  }

  if (UNLIKELY(read_consume(ctx).type != ScriptTokenType_CurlyClose)) {
    return read_emit_err(ctx, ScriptError_UnterminatedBlock, start), read_fail_structural(ctx);
  }

  return expr;
}

static ScriptExpr read_expr_scope_single(ScriptReadContext* ctx, const OpPrecedence prec) {
  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  const ScriptExpr expr = read_expr(ctx, prec);

  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  return expr;
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptExpr read_expr_paren(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptExpr expr = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(sentinel_check(expr))) {
    return read_fail_structural(ctx);
  }
  const ScriptToken closeToken = read_consume(ctx);
  if (UNLIKELY(closeToken.type != ScriptTokenType_ParenClose)) {
    read_emit_err(ctx, ScriptError_UnclosedParenthesizedExpr, start);
    return read_fail_structural(ctx);
  }
  return expr;
}

static bool read_is_args_end(const ScriptTokenType type) {
  return type == ScriptTokenType_End || type == ScriptTokenType_ParenClose;
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static i32 read_args(
    ScriptReadContext* ctx,
    ScriptExpr         outExprs[script_args_max],
    ScriptRange        outRanges[script_args_max]) {
  i32  count = 0;
  bool valid = true;

  ScriptPos argsStart = read_pos_current(ctx);

  if (read_is_args_end(read_peek(ctx).type)) {
    goto ArgEnd; // Empty argument list.
  }

ArgNext:;
  const ScriptPos argStart = read_pos_current(ctx);
  if (UNLIKELY(count == script_args_max)) {
    return read_emit_err(ctx, ScriptError_ArgumentCountExceedsMaximum, argsStart), -1;
  }
  if (UNLIKELY(read_consume_if(ctx, ScriptTokenType_ParenClose))) {
    return read_emit_err(ctx, ScriptError_MissingPrimaryExpr, argStart), -1;
  }
  const ScriptExpr arg = read_expr(ctx, OpPrecedence_None);
  valid &= !sentinel_check(arg);
  outExprs[count]  = arg;
  outRanges[count] = read_range_current(ctx, argStart);
  ++count;

  if (read_consume_if(ctx, ScriptTokenType_Comma)) {
    goto ArgNext;
  }

ArgEnd:
  if (!valid) {
    return -1;
  }
  if (UNLIKELY(read_consume(ctx).type != ScriptTokenType_ParenClose)) {
    return read_emit_err(ctx, ScriptError_UnterminatedArgumentList, argsStart), -1;
  }
  return count;
}

static ScriptExpr read_expr_var_declare(ScriptReadContext* ctx) {
  const ScriptPos   startPos = read_pos_current(ctx);
  const ScriptToken token    = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_Identifier)) {
    return read_emit_err(ctx, ScriptError_VarIdInvalid, startPos), read_fail_structural(ctx);
  }
  if (script_builtin_const_lookup(token.val_identifier)) {
    read_emit_err(ctx, ScriptError_VarIdConflicts, startPos), read_fail_semantic(ctx);
  }
  if (read_var_lookup(ctx, token.val_identifier)) {
    read_emit_err(ctx, ScriptError_VarIdConflicts, startPos), read_fail_semantic(ctx);
  }

  const ScriptRange idRange = read_range_current(ctx, startPos);

  ScriptExpr valExpr;
  if (read_consume_if(ctx, ScriptTokenType_Eq)) {
    const ScriptSection prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
    valExpr                         = read_expr(ctx, OpPrecedence_Assignment);
    ctx->section                    = prevSection;
    if (UNLIKELY(sentinel_check(valExpr))) {
      return read_fail_structural(ctx);
    }
  } else {
    valExpr = script_add_value_anon(ctx->doc, script_null());
  }

  const ScriptRange range = read_range_current(ctx, startPos);

  ScriptVarId varId;
  if (!read_var_declare(ctx, token.val_identifier, idRange, &varId)) {
    return read_emit_err_range(ctx, ScriptError_VarLimitExceeded, range), read_fail_semantic(ctx);
  }

  return script_add_var_store(ctx->doc, range, varId, valExpr);
}

static ScriptExpr
read_expr_var_lookup(ScriptReadContext* ctx, const StringHash id, const ScriptPos start) {
  const ScriptRange         range   = read_range_current(ctx, start);
  const ScriptBuiltinConst* builtin = script_builtin_const_lookup(id);
  if (builtin) {
    return script_add_value(ctx->doc, range, builtin->val);
  }
  ScriptVarMeta* var = read_var_lookup(ctx, id);
  if (var) {
    var->used = true;
    return script_add_var_load(ctx->doc, range, var->varSlot);
  }
  return read_emit_err_range(ctx, ScriptError_NoVarFoundForId, range), read_fail_semantic(ctx);
}

static ScriptExpr
read_expr_var_assign(ScriptReadContext* ctx, const StringHash id, const ScriptPos start) {
  const ScriptSection prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
  const ScriptExpr    expr        = read_expr(ctx, OpPrecedence_Assignment);
  ctx->section                    = prevSection;
  if (UNLIKELY(sentinel_check(expr))) {
    return read_fail_structural(ctx);
  }

  const ScriptVarMeta* var = read_var_lookup(ctx, id);
  if (UNLIKELY(!var)) {
    return read_emit_err(ctx, ScriptError_NoVarFoundForId, start), read_fail_semantic(ctx);
  }

  const ScriptRange range = read_range_current(ctx, start);
  return script_add_var_store(ctx->doc, range, var->varSlot, expr);
}

static ScriptExpr read_expr_var_modify(
    ScriptReadContext*    ctx,
    const StringHash      id,
    const ScriptTokenType type,
    const ScriptPos       start) {
  const ScriptSection   prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
  const ScriptIntrinsic intr        = token_op_binary_modify(type);
  const ScriptExpr      val         = token_intr_rhs_scope(intr)
                                          ? read_expr_scope_single(ctx, OpPrecedence_Assignment)
                                          : read_expr(ctx, OpPrecedence_Assignment);
  ctx->section                      = prevSection;
  if (UNLIKELY(sentinel_check(val))) {
    return read_fail_structural(ctx);
  }

  ScriptVarMeta* var = read_var_lookup(ctx, id);
  if (UNLIKELY(!var)) {
    return read_emit_err(ctx, ScriptError_NoVarFoundForId, start), read_fail_semantic(ctx);
  }

  var->used = true;

  const ScriptRange range      = read_range_current(ctx, start);
  const ScriptExpr  loadExpr   = script_add_var_load(ctx->doc, range, var->varSlot);
  const ScriptExpr  intrArgs[] = {loadExpr, val};
  const ScriptExpr  intrExpr   = script_add_intrinsic(ctx->doc, range, intr, intrArgs);
  return script_add_var_store(ctx->doc, range, var->varSlot, intrExpr);
}

static ScriptExpr
read_expr_mem_store(ScriptReadContext* ctx, const StringHash key, const ScriptPos start) {
  const ScriptSection prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
  const ScriptExpr    val         = read_expr(ctx, OpPrecedence_Assignment);
  ctx->section                    = prevSection;

  if (UNLIKELY(sentinel_check(val))) {
    return read_fail_structural(ctx);
  }
  const ScriptRange range = read_range_current(ctx, start);
  return script_add_mem_store(ctx->doc, range, key, val);
}

static ScriptExpr read_expr_mem_modify(
    ScriptReadContext*    ctx,
    const StringHash      key,
    const ScriptTokenType type,
    const ScriptPos       start) {
  const ScriptSection   prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
  const ScriptIntrinsic intr        = token_op_binary_modify(type);
  const ScriptExpr      val         = token_intr_rhs_scope(intr)
                                          ? read_expr_scope_single(ctx, OpPrecedence_Assignment)
                                          : read_expr(ctx, OpPrecedence_Assignment);
  ctx->section                      = prevSection;
  if (UNLIKELY(sentinel_check(val))) {
    return read_fail_structural(ctx);
  }
  const ScriptRange range      = read_range_current(ctx, start);
  const ScriptExpr  loadExpr   = script_add_mem_load(ctx->doc, range, key);
  const ScriptExpr  intrArgs[] = {loadExpr, val};
  const ScriptExpr  intrExpr   = script_add_intrinsic(ctx->doc, range, intr, intrArgs);
  return script_add_mem_store(ctx->doc, range, key, intrExpr);
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptExpr
read_expr_call(ScriptReadContext* ctx, const StringHash id, const ScriptRange idRange) {
  ScriptExpr  args[script_args_max];
  ScriptRange argRanges[script_args_max];
  const i32   argCount = read_args(ctx, args, argRanges);
  if (UNLIKELY(argCount < 0)) {
    return read_fail_structural(ctx);
  }
  const ScriptRange callRange = read_range_current(ctx, idRange.start);

  const ScriptBuiltinFunc* builtin = script_builtin_func_lookup(id, (u32)argCount);
  if (builtin) {
    return script_add_intrinsic(ctx->doc, callRange, builtin->intr, args);
  }
  if (script_builtin_func_exists(id)) {
    read_emit_err_range(ctx, ScriptError_IncorrectArgCountForBuiltinFunc, callRange);
    return read_fail_semantic(ctx);
  }

  if (ctx->binder) {
    const ScriptBinderSlot externFunc = script_binder_lookup(ctx->binder, id);
    if (!sentinel_check(externFunc)) {
      return script_add_extern(ctx->doc, callRange, externFunc, args, (u32)argCount);
    }
  }

  return read_emit_err_range(ctx, ScriptError_NoFuncFoundForId, idRange), read_fail_semantic(ctx);
}

static void read_emit_static_condition(
    ScriptReadContext* ctx, const ScriptExpr expr, const ScriptRange exprRange) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagType_Warning)) {
    return;
  }
  if (script_expr_static(ctx->doc, expr)) {
    const ScriptDiag staticConditionDiag = {
        .type  = ScriptDiagType_Warning,
        .error = ScriptError_ConditionExprStatic,
        .range = read_range_trim(ctx, exprRange),
    };
    script_diag_push(ctx->diags, &staticConditionDiag);
  }
}

static ScriptExpr read_expr_if(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_ParenOpen)) {
    return read_emit_err(ctx, ScriptError_InvalidIf, start), read_fail_structural(ctx);
  }

  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  ScriptExpr  conditions[script_args_max];
  ScriptRange conditionRanges[script_args_max];
  const i32   conditionCount = read_args(ctx, conditions, conditionRanges);
  if (UNLIKELY(conditionCount < 0)) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  if (UNLIKELY(conditionCount != 1)) {
    read_emit_err(ctx, ScriptError_InvalidConditionCount, start);
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  read_emit_static_condition(ctx, conditions[0], conditionRanges[0]);

  const ScriptPos blockStart = read_pos_current(ctx);

  if (read_consume(ctx).type != ScriptTokenType_CurlyOpen) {
    read_emit_err(ctx, ScriptError_BlockExpected, blockStart);
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  const ScriptExpr b1 = read_expr_scope_block(ctx);
  if (UNLIKELY(sentinel_check(b1))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

  const ScriptPos elseStart = read_pos_current(ctx);

  ScriptExpr b2;
  if (read_consume_if(ctx, ScriptTokenType_Else)) {
    const ScriptPos elseBlockStart = read_pos_current(ctx);
    if (read_consume_if(ctx, ScriptTokenType_CurlyOpen)) {
      b2 = read_expr_scope_block(ctx);
      if (UNLIKELY(sentinel_check(b2))) {
        return read_scope_pop(ctx), read_fail_structural(ctx);
      }
    } else if (read_consume_if(ctx, ScriptTokenType_If)) {
      b2 = read_expr_if(ctx, elseBlockStart);
      if (UNLIKELY(sentinel_check(b2))) {
        return read_scope_pop(ctx), read_fail_structural(ctx);
      }
    } else {
      read_emit_err(ctx, ScriptError_BlockOrIfExpected, elseStart);
      return read_scope_pop(ctx), read_fail_structural(ctx);
    }
  } else {
    b2 = script_add_value_anon(ctx->doc, script_null());
  }

  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  const ScriptRange range      = read_range_current(ctx, start);
  const ScriptExpr  intrArgs[] = {conditions[0], b1, b2};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Select, intrArgs);
}

static ScriptExpr read_expr_while(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_ParenOpen)) {
    return read_emit_err(ctx, ScriptError_InvalidWhileLoop, start), read_fail_structural(ctx);
  }

  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  ScriptExpr  conditions[script_args_max];
  ScriptRange conditionRanges[script_args_max];
  const i32   conditionCount = read_args(ctx, conditions, conditionRanges);
  if (UNLIKELY(conditionCount < 0)) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  if (UNLIKELY(conditionCount != 1)) {
    read_emit_err(ctx, ScriptError_InvalidConditionCount, start);
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  read_emit_static_condition(ctx, conditions[0], conditionRanges[0]);

  const ScriptPos blockStart = read_pos_current(ctx);

  if (UNLIKELY(read_consume(ctx).type != ScriptTokenType_CurlyOpen)) {
    read_emit_err(ctx, ScriptError_BlockExpected, blockStart);
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

  const ScriptSection prevSection = read_section_add(ctx, ScriptSection_InsideLoop);
  const ScriptExpr    body        = read_expr_scope_block(ctx);
  ctx->section                    = prevSection;

  if (UNLIKELY(sentinel_check(body))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  const ScriptRange range = read_range_current(ctx, start);
  // NOTE: Setup and Increment loop parts are not used in while loops.
  const ScriptExpr setupExpr  = script_add_value_anon(ctx->doc, script_null());
  const ScriptExpr incrExpr   = script_add_value_anon(ctx->doc, script_null());
  const ScriptExpr intrArgs[] = {setupExpr, conditions[0], incrExpr, body};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Loop, intrArgs);
}

static void read_emit_static_for_comp(
    ScriptReadContext* ctx, const ScriptExpr expr, const ScriptRange exprRange) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagType_Warning)) {
    return;
  }
  if (script_expr_static(ctx->doc, expr)) {
    const ScriptDiag staticForCompDiag = {
        .type  = ScriptDiagType_Warning,
        .error = ScriptError_ForLoopCompStatic,
        .range = read_range_trim(ctx, exprRange),
    };
    script_diag_push(ctx->diags, &staticForCompDiag);
  }
}

typedef enum {
  ReadForComp_Setup,
  ReadForComp_Condition,
  ReadForComp_Increment,
} ReadForComp;

static ScriptExpr read_expr_for_comp(ScriptReadContext* ctx, const ReadForComp comp) {
  static const ScriptTokenType g_endTokens[] = {
      [ReadForComp_Setup]     = ScriptTokenType_Semicolon,
      [ReadForComp_Condition] = ScriptTokenType_Semicolon,
      [ReadForComp_Increment] = ScriptTokenType_ParenClose,
  };
  const ScriptPos start = read_pos_current(ctx);
  ScriptExpr      res;
  if (read_peek(ctx).type == g_endTokens[comp]) {
    const ScriptVal skipVal = comp == ReadForComp_Condition ? script_bool(true) : script_null();
    res                     = script_add_value_anon(ctx->doc, skipVal);
  } else if (UNLIKELY(read_peek(ctx).type == ScriptTokenType_ParenClose)) {
    return read_emit_err(ctx, ScriptError_ForLoopCompMissing, start), read_fail_structural(ctx);
  } else {
    res = read_expr(ctx, OpPrecedence_None);
    if (UNLIKELY(sentinel_check(res))) {
      return read_fail_structural(ctx);
    }
    read_emit_static_for_comp(ctx, res, read_range_current(ctx, start));
  }
  if (UNLIKELY(read_consume(ctx).type != g_endTokens[comp])) {
    const ScriptError err = comp == ReadForComp_Increment ? ScriptError_InvalidForLoop
                                                          : ScriptError_ForLoopSeparatorMissing;
    return read_emit_err(ctx, err, start), read_fail_structural(ctx);
  }
  return res;
}

static ScriptExpr read_expr_for(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_ParenOpen)) {
    return read_emit_err(ctx, ScriptError_InvalidForLoop, start), read_fail_structural(ctx);
  }

  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  const ScriptExpr setupExpr = read_expr_for_comp(ctx, ReadForComp_Setup);
  if (UNLIKELY(sentinel_check(setupExpr))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  const ScriptExpr condExpr = read_expr_for_comp(ctx, ReadForComp_Condition);
  if (UNLIKELY(sentinel_check(condExpr))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  const ScriptExpr incrExpr = read_expr_for_comp(ctx, ReadForComp_Increment);
  if (UNLIKELY(sentinel_check(incrExpr))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

  const ScriptPos blockStart = read_pos_current(ctx);

  if (UNLIKELY(read_consume(ctx).type != ScriptTokenType_CurlyOpen)) {
    read_emit_err(ctx, ScriptError_BlockExpected, blockStart);
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

  const ScriptSection prevSection = read_section_add(ctx, ScriptSection_InsideLoop);
  const ScriptExpr    body        = read_expr_scope_block(ctx);
  ctx->section                    = prevSection;

  if (UNLIKELY(sentinel_check(body))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  const ScriptRange range      = read_range_current(ctx, start);
  const ScriptExpr  intrArgs[] = {setupExpr, condExpr, incrExpr, body};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Loop, intrArgs);
}

static ScriptExpr read_expr_select(ScriptReadContext* ctx, const ScriptExpr condition) {
  const ScriptPos start = read_pos_current(ctx);

  const ScriptExpr b1 = read_expr_scope_single(ctx, OpPrecedence_Conditional);
  if (UNLIKELY(sentinel_check(b1))) {
    return read_fail_structural(ctx);
  }

  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_Colon)) {
    read_emit_err(ctx, ScriptError_MissingColonInSelectExpr, start);
    return read_fail_structural(ctx);
  }

  const ScriptExpr b2 = read_expr_scope_single(ctx, OpPrecedence_Conditional);
  if (UNLIKELY(sentinel_check(b2))) {
    return read_fail_structural(ctx);
  }

  const ScriptRange range      = read_range_current(ctx, start);
  const ScriptExpr  intrArgs[] = {condition, b1, b2};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Select, intrArgs);
}

static bool read_is_return_separator(const ScriptTokenType tokenType) {
  switch (tokenType) {
  case ScriptTokenType_Newline:
  case ScriptTokenType_Semicolon:
  case ScriptTokenType_CurlyClose:
  case ScriptTokenType_End:
    return true;
  default:
    return false;
  }
}

static ScriptExpr read_expr_return(ScriptReadContext* ctx, const ScriptPos start) {
  ScriptToken nextToken;
  script_lex(ctx->input, null, &nextToken, ScriptLexFlags_IncludeNewlines);

  ScriptExpr retExpr;
  if (read_is_return_separator(nextToken.type)) {
    retExpr = script_add_value_anon(ctx->doc, script_null());
  } else {
    const ScriptSection prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
    retExpr                         = read_expr(ctx, OpPrecedence_Assignment);
    ctx->section                    = prevSection;
    if (UNLIKELY(sentinel_check(retExpr))) {
      return read_fail_structural(ctx);
    }
  }

  const ScriptRange range      = read_range_current(ctx, start);
  const ScriptExpr  intrArgs[] = {retExpr};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Return, intrArgs);
}

static ScriptExpr read_expr_primary(ScriptReadContext* ctx) {
  const ScriptPos start = read_pos_current(ctx);

  ScriptToken token;
  ctx->input = script_lex(ctx->input, g_stringtable, &token, ScriptLexFlags_None);

  switch (token.type) {
  /**
   * Parenthesized expression.
   */
  case ScriptTokenType_ParenOpen:
    return read_expr_paren(ctx, start);
  /**
   * Scope.
   */
  case ScriptTokenType_CurlyOpen:
    return read_expr_scope_block(ctx);
  /**
   * Keywords.
   */
  case ScriptTokenType_If:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowIf)) {
      return read_emit_err(ctx, ScriptError_IfNotAllowed, start), read_fail_structural(ctx);
    }
    return read_expr_if(ctx, start);
  case ScriptTokenType_While:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowLoop)) {
      return read_emit_err(ctx, ScriptError_LoopNotAllowed, start), read_fail_structural(ctx);
    }
    return read_expr_while(ctx, start);
  case ScriptTokenType_For:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowLoop)) {
      return read_emit_err(ctx, ScriptError_LoopNotAllowed, start), read_fail_structural(ctx);
    }
    return read_expr_for(ctx, start);
  case ScriptTokenType_Var:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowVarDeclare)) {
      return read_emit_err(ctx, ScriptError_VarDeclareNotAllowed, start), read_fail_structural(ctx);
    }
    return read_expr_var_declare(ctx);
  case ScriptTokenType_Continue: {
    const ScriptRange range = read_range_current(ctx, start);
    if (UNLIKELY(!(ctx->section & ScriptSection_InsideLoop))) {
      return read_emit_err_range(ctx, ScriptError_OnlyValidInLoop, range), read_fail_semantic(ctx);
    }
    return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Continue, null);
  }
  case ScriptTokenType_Break: {
    const ScriptRange range = read_range_current(ctx, start);
    if (UNLIKELY(!(ctx->section & ScriptSection_InsideLoop))) {
      return read_emit_err_range(ctx, ScriptError_OnlyValidInLoop, range), read_fail_semantic(ctx);
    }
    return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Break, null);
  }
  case ScriptTokenType_Return:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowReturn)) {
      return read_emit_err(ctx, ScriptError_ReturnNotAllowed, start), read_fail_structural(ctx);
    }
    return read_expr_return(ctx, start);
  /**
   * Identifiers.
   */
  case ScriptTokenType_Identifier: {
    const ScriptRange idRange = read_range_current(ctx, start);
    ScriptToken       nextToken;
    const String      remInput = script_lex(ctx->input, null, &nextToken, ScriptLexFlags_None);
    switch (nextToken.type) {
    case ScriptTokenType_ParenOpen:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_call(ctx, token.val_identifier, idRange);
    case ScriptTokenType_Eq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_var_assign(ctx, token.val_identifier, start);
    case ScriptTokenType_PlusEq:
    case ScriptTokenType_MinusEq:
    case ScriptTokenType_StarEq:
    case ScriptTokenType_SlashEq:
    case ScriptTokenType_PercentEq:
    case ScriptTokenType_QMarkQMarkEq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_var_modify(ctx, token.val_key, nextToken.type, start);
    default:
      return read_expr_var_lookup(ctx, token.val_identifier, start);
    }
  }
  /**
   * Unary operators.
   */
  case ScriptTokenType_Minus:
  case ScriptTokenType_Bang: {
    const ScriptExpr val = read_expr(ctx, OpPrecedence_Unary);
    if (UNLIKELY(sentinel_check(val))) {
      return read_fail_structural(ctx);
    }
    const ScriptRange     range      = read_range_current(ctx, start);
    const ScriptIntrinsic intr       = token_op_unary(token.type);
    const ScriptExpr      intrArgs[] = {val};
    return script_add_intrinsic(ctx->doc, range, intr, intrArgs);
  }
  /**
   * Literals.
   */
  case ScriptTokenType_Number: {
    const ScriptRange range = read_range_current(ctx, start);
    return script_add_value(ctx->doc, range, script_number(token.val_number));
  }
  case ScriptTokenType_String: {
    const ScriptRange range = read_range_current(ctx, start);
    return script_add_value(ctx->doc, range, script_string(token.val_string));
  }
  /**
   * Memory access.
   */
  case ScriptTokenType_Key: {
    // TODO: Should failing to track be an error? Currently these are only used to report symbols.
    read_track_mem_key(ctx, token.val_key);

    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, null, &nextToken, ScriptLexFlags_None);
    switch (nextToken.type) {
    case ScriptTokenType_Eq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_mem_store(ctx, token.val_key, start);
    case ScriptTokenType_PlusEq:
    case ScriptTokenType_MinusEq:
    case ScriptTokenType_StarEq:
    case ScriptTokenType_SlashEq:
    case ScriptTokenType_PercentEq:
    case ScriptTokenType_QMarkQMarkEq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_mem_modify(ctx, token.val_key, nextToken.type, start);
    default: {
      const ScriptRange range = read_range_current(ctx, start);
      return script_add_mem_load(ctx->doc, range, token.val_key);
    }
    }
  }
  /**
   * Lex errors.
   */
  case ScriptTokenType_Semicolon:
    return read_emit_err(ctx, ScriptError_UnexpectedSemicolon, start), read_fail_structural(ctx);
  case ScriptTokenType_Error:
    return read_emit_err(ctx, token.val_error, start), read_fail_structural(ctx);
  case ScriptTokenType_End:
    return read_emit_err(ctx, ScriptError_MissingPrimaryExpr, start), read_fail_structural(ctx);
  default:
    return read_emit_err(ctx, ScriptError_InvalidPrimaryExpr, start), read_fail_structural(ctx);
  }
}

static ScriptExpr read_expr(ScriptReadContext* ctx, const OpPrecedence minPrecedence) {
  ++ctx->recursionDepth;
  if (UNLIKELY(ctx->recursionDepth >= script_depth_max)) {
    read_emit_err(ctx, ScriptError_RecursionLimitExceeded, read_pos_current(ctx));
    return read_fail_structural(ctx);
  }

  ScriptPos  resStart = read_pos_current(ctx);
  ScriptExpr res      = read_expr_primary(ctx);
  if (UNLIKELY(sentinel_check(res))) {
    return read_fail_structural(ctx);
  }
  ScriptRange resRange = read_range_current(ctx, resStart);

  /**
   * Test if the next token is an operator with higher precedence.
   */
  while (true) {
    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, g_stringtable, &nextToken, ScriptLexFlags_None);

    const OpPrecedence opPrecedence = op_precedence(nextToken.type);
    if (!opPrecedence || opPrecedence <= minPrecedence) {
      break;
    }
    /**
     * Next token is an operator with a high enough precedence.
     * Consume the token and recurse down the right hand side.
     */
    ctx->input = remInput; // Consume the 'nextToken'.

    /**
     * Binary / Ternary expressions.
     */
    switch (nextToken.type) {
    case ScriptTokenType_QMark: {
      read_emit_static_condition(ctx, res, resRange);

      resStart = read_pos_current(ctx);
      res      = read_expr_select(ctx, res);
      if (UNLIKELY(sentinel_check(res))) {
        return read_fail_structural(ctx);
      }
      resRange = read_range_current(ctx, resStart);
    } break;
    case ScriptTokenType_EqEq:
    case ScriptTokenType_BangEq:
    case ScriptTokenType_Le:
    case ScriptTokenType_LeEq:
    case ScriptTokenType_Gt:
    case ScriptTokenType_GtEq:
    case ScriptTokenType_Plus:
    case ScriptTokenType_Minus:
    case ScriptTokenType_Star:
    case ScriptTokenType_Slash:
    case ScriptTokenType_Percent:
    case ScriptTokenType_AmpAmp:
    case ScriptTokenType_PipePipe:
    case ScriptTokenType_QMarkQMark: {
      resStart                   = read_pos_current(ctx);
      const ScriptIntrinsic intr = token_op_binary(nextToken.type);
      const ScriptExpr rhs = token_intr_rhs_scope(intr) ? read_expr_scope_single(ctx, opPrecedence)
                                                        : read_expr(ctx, opPrecedence);
      if (UNLIKELY(sentinel_check(rhs))) {
        return read_fail_structural(ctx);
      }
      const ScriptExpr intrArgs[] = {res, rhs};
      resRange                    = read_range_current(ctx, resStart);
      res                         = script_add_intrinsic(ctx->doc, resRange, intr, intrArgs);
    } break;
    default:
      diag_assert_fail("Invalid operator token");
      UNREACHABLE
    }
  }
  --ctx->recursionDepth;
  return res;
}

static void read_sym_push_keywords(ScriptReadContext* ctx) {
  if (!ctx->syms) {
    return;
  }
  for (u32 i = 0; i != script_lex_keyword_count(); ++i) {
    const ScriptSym sym = {
        .type       = ScriptSymType_Keyword,
        .label      = script_lex_keyword_data()[i].id,
        .validRange = read_range_full(ctx),
    };
    script_sym_push(ctx->syms, &sym);
  }
}

static void read_sym_push_builtin(ScriptReadContext* ctx) {
  if (!ctx->syms) {
    return;
  }
  for (u32 i = 0; i != g_scriptBuiltinConstCount; ++i) {
    const ScriptSym sym = {
        .type       = ScriptSymType_BuiltinConstant,
        .label      = g_scriptBuiltinConsts[i].id,
        .validRange = read_range_full(ctx),
    };
    script_sym_push(ctx->syms, &sym);
  }
  for (u32 i = 0; i != g_scriptBuiltinFuncCount; ++i) {
    const ScriptSym sym = {
        .type       = ScriptSymType_BuiltinFunction,
        .label      = g_scriptBuiltinFuncs[i].id,
        .validRange = read_range_full(ctx),
    };
    script_sym_push(ctx->syms, &sym);
  }
}

static void read_sym_push_extern(ScriptReadContext* ctx) {
  if (!ctx->syms || !ctx->binder) {
    return;
  }
  ScriptBinderSlot itr = script_binder_first(ctx->binder);
  for (; !sentinel_check(itr); itr = script_binder_next(ctx->binder, itr)) {
    const ScriptSym sym = {
        .type       = ScriptSymType_ExternFunction,
        .label      = script_binder_name_str(ctx->binder, itr),
        .validRange = read_range_full(ctx),
    };
    script_sym_push(ctx->syms, &sym);
  }
}

static void read_sym_push_mem_keys(ScriptReadContext* ctx) {
  if (!ctx->syms) {
    return;
  }
  for (u32 i = 0; i != script_tracked_mem_keys_max; ++i) {
    if (!ctx->trackedMemKeys[i]) {
      break;
    }
    // TODO: Using the global string-table for this is kinda questionable.
    const String keyStr = stringtable_lookup(g_stringtable, ctx->trackedMemKeys[i]);
    if (!string_is_empty(keyStr)) {
      const ScriptSym sym = {
          .type       = ScriptSymType_MemoryKey,
          .label      = fmt_write_scratch("${}", fmt_text(keyStr)),
          .validRange = read_range_full(ctx),
      };
      script_sym_push(ctx->syms, &sym);
    }
  }
}

static void script_link_binder(ScriptDoc* doc, const ScriptBinder* binder) {
  const ScriptBinderSignature signature = script_binder_sig(binder);
  if (doc->binderSignature && doc->binderSignature != signature) {
    diag_assert_fail("ScriptDoc was already used with a different (and incompatible binder)");
  }
  doc->binderSignature = signature;
}

static void script_read_init() {
  static bool           g_init;
  static ThreadSpinLock g_initLock;
  if (g_init) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_init) {
    script_builtin_init();
    g_init = true;
  }
  thread_spinlock_unlock(&g_initLock);
}

ScriptExpr script_read(
    ScriptDoc*          doc,
    const ScriptBinder* binder,
    const String        src,
    ScriptDiagBag*      diags,
    ScriptSymBag*       syms) {
  script_read_init();

  if (binder) {
    script_link_binder(doc, binder);
  }

  ScriptScope       scopeRoot = {0};
  ScriptReadContext ctx       = {
            .doc        = doc,
            .binder     = binder,
            .diags      = diags,
            .syms       = syms,
            .input      = src,
            .inputTotal = src,
            .scopeRoot  = &scopeRoot,
  };
  read_var_free_all(&ctx);

  read_sym_push_keywords(&ctx);
  read_sym_push_builtin(&ctx);
  read_sym_push_extern(&ctx);

  const ScriptExpr expr = read_expr_block(&ctx, ScriptBlockType_Implicit);
  if (!sentinel_check(expr)) {
    diag_assert_msg(read_peek(&ctx).type == ScriptTokenType_End, "Not all input consumed");
  }

  read_sym_push_mem_keys(&ctx);
  read_sym_push_vars(&ctx, &scopeRoot);
  read_emit_unused_vars(&ctx, &scopeRoot);

  const bool fail = sentinel_check(expr) || (ctx.flags & ScriptReadFlags_ProgramInvalid) != 0;
#ifndef VOLO_FAST
  if (diags) {
    const bool hasErrDiag = script_diag_count(diags, ScriptDiagFilter_Error);
    diag_assert_msg(!fail || hasErrDiag, "No error diagnostic was produced for a failed read");
    diag_assert_msg(fail || !hasErrDiag, "Error diagnostic was produced for a successful read");
  }
#endif
  return fail ? script_expr_sentinel : expr;
}
