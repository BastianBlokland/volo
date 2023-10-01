#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_utf8.h"
#include "script_binder.h"
#include "script_lex.h"
#include "script_read.h"

#include "doc_internal.h"

#define script_depth_max 25
#define script_block_size_max 128
#define script_args_max 10
#define script_builtin_consts_max 32
#define script_builtin_funcs_max 32

typedef struct {
  StringHash idHash;
  ScriptVal  val;
} ScriptBuiltinConst;

static ScriptBuiltinConst g_scriptBuiltinConsts[script_builtin_consts_max];
static u32                g_scriptBuiltinConstCount;

static void script_builtin_const_add(const String id, const ScriptVal val) {
  diag_assert(g_scriptBuiltinConstCount != script_builtin_consts_max);
  g_scriptBuiltinConsts[g_scriptBuiltinConstCount++] = (ScriptBuiltinConst){
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
} ScriptBuiltinFunc;

static ScriptBuiltinFunc g_scriptBuiltinFuncs[script_builtin_funcs_max];
static u32               g_scriptBuiltinFuncCount;

static void script_builtin_func_add(const String id, const ScriptIntrinsic intr) {
  diag_assert(g_scriptBuiltinFuncCount != script_builtin_funcs_max);
  g_scriptBuiltinFuncs[g_scriptBuiltinFuncCount++] = (ScriptBuiltinFunc){
      .idHash   = string_hash(id),
      .argCount = script_intrinsic_arg_count(intr),
      .intr     = intr,
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
} ScriptVarMeta;

typedef struct sScriptScope {
  ScriptVarMeta        vars[script_var_count];
  struct sScriptScope* next;
} ScriptScope;

typedef enum {
  ScriptReadFlags_InsideLoop = 1 << 0,
} ScriptReadFlags;

typedef struct {
  ScriptDoc*          doc;
  const ScriptBinder* binder;
  String              input, inputTotal;
  ScriptScope*        scopeRoot;
  ScriptReadFlags     flags : 16;
  u16                 recursionDepth;
  u8                  varAvailability[bits_to_bytes(script_var_count) + 1]; // Bitmask of free vars.
} ScriptReadContext;

typedef u32 ScriptMarker;

static ScriptMarker script_marker(ScriptReadContext* ctx) {
  return (ScriptMarker)(ctx->inputTotal.size - ctx->input.size);
}

static ScriptMarker script_marker_trim(ScriptReadContext* ctx, const ScriptMarker marker) {
  const String text        = string_consume(ctx->inputTotal, marker);
  const String textTrimmed = script_lex_trim(text);
  return (ScriptMarker)(ctx->inputTotal.size - textTrimmed.size);
}

static ScriptPos script_marker_to_pos(ScriptReadContext* ctx, const ScriptMarker marker) {
  diag_assert(marker <= ctx->inputTotal.size);
  u32 pos  = 0;
  u16 line = 1, column = 1;
  while (pos < marker) {
    const u8 ch = *string_at(ctx->inputTotal, pos);
    switch (ch) {
    case '\n':
      ++pos;
      ++line;
      column = 1;
      break;
    case '\r':
      ++pos;
      break;
    default:
      pos += (u32)math_max(utf8_cp_bytes_from_first(ch), 1);
      ++column;
    }
  }
  return (ScriptPos){.line = line, .column = column};
}

static bool script_var_alloc(ScriptReadContext* ctx, ScriptVarId* out) {
  const usize index = bitset_next(mem_var(ctx->varAvailability), 0);
  if (UNLIKELY(sentinel_check(index))) {
    return false;
  }
  bitset_clear(mem_var(ctx->varAvailability), index);
  *out = (ScriptVarId)index;
  return true;
}

static void script_var_free(ScriptReadContext* ctx, const ScriptVarId var) {
  diag_assert(!bitset_test(mem_var(ctx->varAvailability), var));
  bitset_set(mem_var(ctx->varAvailability), var);
}

static void script_var_free_all(ScriptReadContext* ctx) {
  bitset_set_all(mem_var(ctx->varAvailability), script_var_count);
}

static ScriptScope* script_scope_head(ScriptReadContext* ctx) { return ctx->scopeRoot; }
static ScriptScope* script_scope_tail(ScriptReadContext* ctx) {
  ScriptScope* scope = ctx->scopeRoot;
  for (; scope->next; scope = scope->next)
    ;
  return scope;
}

static void script_scope_push(ScriptReadContext* ctx, ScriptScope* scope) {
  script_scope_tail(ctx)->next = scope;
}

static void script_scope_pop(ScriptReadContext* ctx) {
  ScriptScope* scope = script_scope_tail(ctx);
  diag_assert(scope && scope != ctx->scopeRoot);

  // Remove the scope from the scope list.
  ScriptScope* newTail = ctx->scopeRoot;
  for (; newTail->next != scope; newTail = newTail->next)
    ;
  newTail->next = null;

  // Free all the variables that the scope declared.
  for (u32 i = 0; i != script_var_count; ++i) {
    if (scope->vars[i].id) {
      script_var_free(ctx, scope->vars[i].varSlot);
    }
  }
}

static bool script_var_declare(ScriptReadContext* ctx, const StringHash id, ScriptVarId* out) {
  ScriptScope* scope = script_scope_tail(ctx);
  diag_assert(scope);

  for (u32 i = 0; i != script_var_count; ++i) {
    if (scope->vars[i].id) {
      continue; // Var already in use.
    }
    if (!script_var_alloc(ctx, out)) {
      return false;
    }
    scope->vars[i] = (ScriptVarMeta){.id = id, .varSlot = *out};
    return true;
  }
  return false;
}

static const ScriptVarMeta* script_var_lookup(ScriptReadContext* ctx, const StringHash id) {
  for (ScriptScope* scope = script_scope_head(ctx); scope; scope = scope->next) {
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

static ScriptReadResult read_success(const ScriptExpr expr) {
  return (ScriptReadResult){.type = ScriptResult_Success, .expr = expr};
}

static ScriptReadResult
read_error(ScriptReadContext* ctx, const ScriptResult err, const ScriptMarker start) {
  diag_assert(err != ScriptResult_Success);

  const ScriptMarker startTrimmed = script_marker_trim(ctx, start);
  const ScriptMarker end          = script_marker(ctx);
  return (ScriptReadResult){
      .type       = err,
      .errorStart = script_marker_to_pos(ctx, startTrimmed),
      .errorEnd   = script_marker_to_pos(ctx, end),
  };
}

static bool read_require_separation(ScriptReadContext* ctx, const ScriptExpr expr) {
  switch (script_expr_type(ctx->doc, expr)) {
  case ScriptExprType_Value:
  case ScriptExprType_VarLoad:
  case ScriptExprType_VarStore:
  case ScriptExprType_MemLoad:
  case ScriptExprType_MemStore:
  case ScriptExprType_Extern:
    return true;
  case ScriptExprType_Block:
    return false;
  case ScriptExprType_Intrinsic: {
    const ScriptExprData* data = dynarray_at_t(&ctx->doc->exprData, expr, ScriptExprData);
    switch (data->data_intrinsic.intrinsic) {
    case ScriptIntrinsic_If:
    case ScriptIntrinsic_While:
    case ScriptIntrinsic_For:
      return false;
    default:
      return true;
    }
  }
  case ScriptExprType_Count:
    break;
  }
  diag_assert_fail("Invalid expr");
  UNREACHABLE
}

static ScriptReadResult read_expr(ScriptReadContext*, OpPrecedence minPrecedence);

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

static ScriptReadResult read_expr_block(ScriptReadContext* ctx, const ScriptBlockType blockType) {
  const ScriptMarker blockStart = script_marker(ctx);

  ScriptExpr exprs[script_block_size_max];
  u32        exprCount = 0;

  if (read_is_block_end(read_peek(ctx).type, blockType)) {
    goto BlockEnd; // Empty block.
  }

BlockNext:
  if (UNLIKELY(exprCount == script_block_size_max)) {
    return read_error(ctx, ScriptResult_BlockSizeExceedsMaximum, blockStart);
  }
  const ScriptMarker     exprStart = script_marker(ctx);
  const ScriptReadResult exprRes   = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(exprRes.type != ScriptResult_Success)) {
    return exprRes;
  }
  exprs[exprCount++] = exprRes.expr;

  if (read_is_block_end(read_peek(ctx).type, blockType)) {
    goto BlockEnd;
  }
  if (read_require_separation(ctx, exprRes.expr)) {
    if (!read_consume_if(ctx, ScriptTokenType_SemiColon)) {
      return read_error(ctx, ScriptResult_MissingSemicolon, exprStart);
    }
  }
  if (!read_is_block_end(read_peek(ctx).type, blockType)) {
    goto BlockNext;
  }

BlockEnd:
  switch (exprCount) {
  case 0:
    return read_success(script_add_value(ctx->doc, script_null()));
  case 1:
    return read_success(exprs[0]);
  default:
    return read_success(script_add_block(ctx->doc, exprs, exprCount));
  }
}

/**
 * NOTE: Caller is expected to consume the opening curly-brace.
 */
static ScriptReadResult read_expr_scope_block(ScriptReadContext* ctx) {
  const ScriptMarker start = script_marker(ctx);

  ScriptScope scope = {0};
  script_scope_push(ctx, &scope);

  const ScriptReadResult res = read_expr_block(ctx, ScriptBlockType_Explicit);

  diag_assert(&scope == script_scope_tail(ctx));
  script_scope_pop(ctx);

  if (UNLIKELY(res.type != ScriptResult_Success)) {
    return res;
  }

  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_CurlyClose)) {
    return read_error(ctx, ScriptResult_UnterminatedBlock, start);
  }

  return res;
}

static ScriptReadResult read_expr_scope_single(ScriptReadContext* ctx, const OpPrecedence prec) {
  ScriptScope scope = {0};
  script_scope_push(ctx, &scope);

  const ScriptReadResult res = read_expr(ctx, prec);

  diag_assert(&scope == script_scope_tail(ctx));
  script_scope_pop(ctx);

  return res;
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptReadResult read_expr_paren(ScriptReadContext* ctx, const ScriptMarker start) {
  const ScriptReadResult res = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(res.type != ScriptResult_Success)) {
    return res;
  }
  const ScriptToken closeToken = read_consume(ctx);
  if (UNLIKELY(closeToken.type != ScriptTokenType_ParenClose)) {
    return read_error(ctx, ScriptResult_UnclosedParenthesizedExpression, start);
  }
  return res;
}

typedef struct {
  ScriptResult type;
  u32          argCount;
} ScriptArgsResult;

#define script_args_success(_COUNT_)                                                               \
  (ScriptArgsResult) { .type = ScriptResult_Success, .argCount = (_COUNT_) }

#define script_args_err(_ERR_)                                                                     \
  (ScriptArgsResult) { .type = (_ERR_) }

static bool read_is_args_end(const ScriptTokenType type) {
  return type == ScriptTokenType_End || type == ScriptTokenType_ParenClose;
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptArgsResult read_args(ScriptReadContext* ctx, ScriptExpr out[script_args_max]) {
  u32 count = 0;

  if (read_is_args_end(read_peek(ctx).type)) {
    goto ArgEnd; // Empty argument list.
  }

ArgNext:
  if (UNLIKELY(count == script_args_max)) {
    return script_args_err(ScriptResult_ArgumentCountExceedsMaximum);
  }
  const ScriptReadResult arg = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(arg.type != ScriptResult_Success)) {
    return script_args_err(arg.type);
  }
  out[count++] = arg.expr;

  if (read_consume_if(ctx, ScriptTokenType_Comma)) {
    goto ArgNext;
  }

ArgEnd:;
  const ScriptToken endToken = read_consume(ctx);
  if (UNLIKELY(endToken.type != ScriptTokenType_ParenClose)) {
    return script_args_err(ScriptResult_UnterminatedArgumentList);
  }
  return script_args_success(count);
}

static ScriptReadResult read_expr_var_declare(ScriptReadContext* ctx, const ScriptMarker start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_Identifier)) {
    return read_error(ctx, ScriptResult_VariableIdentifierMissing, start);
  }
  if (script_builtin_const_lookup(token.val_identifier)) {
    return read_error(ctx, ScriptResult_VariableIdentifierConflicts, start);
  }
  if (script_var_lookup(ctx, token.val_identifier)) {
    return read_error(ctx, ScriptResult_VariableIdentifierConflicts, start);
  }
  if (script_builtin_func_exists(token.val_identifier)) {
    return read_error(ctx, ScriptResult_VariableIdentifierConflicts, start);
  }
  if (ctx->binder && !sentinel_check(script_binder_lookup(ctx->binder, token.val_identifier))) {
    return read_error(ctx, ScriptResult_VariableIdentifierConflicts, start);
  }

  ScriptExpr valExpr;
  if (read_consume_if(ctx, ScriptTokenType_Eq)) {
    const ScriptReadResult res = read_expr(ctx, OpPrecedence_Assignment);
    if (UNLIKELY(res.type != ScriptResult_Success)) {
      return res;
    }
    valExpr = res.expr;
  } else {
    valExpr = script_add_value(ctx->doc, script_null());
  }

  ScriptVarId varId;
  if (!script_var_declare(ctx, token.val_identifier, &varId)) {
    return read_error(ctx, ScriptResult_VariableLimitExceeded, start);
  }

  return read_success(script_add_var_store(ctx->doc, varId, valExpr));
}

static ScriptReadResult read_expr_var_lookup(
    ScriptReadContext* ctx, const StringHash identifier, const ScriptMarker start) {
  const ScriptBuiltinConst* builtin = script_builtin_const_lookup(identifier);
  if (builtin) {
    return read_success(script_add_value(ctx->doc, builtin->val));
  }
  const ScriptVarMeta* var = script_var_lookup(ctx, identifier);
  if (var) {
    return read_success(script_add_var_load(ctx->doc, var->varSlot));
  }
  return read_error(ctx, ScriptResult_NoVariableFoundForIdentifier, start);
}

static ScriptReadResult read_expr_var_assign(
    ScriptReadContext* ctx, const StringHash identifier, const ScriptMarker start) {
  const ScriptVarMeta* var = script_var_lookup(ctx, identifier);
  if (UNLIKELY(!var)) {
    return read_error(ctx, ScriptResult_NoVariableFoundForIdentifier, start);
  }

  const ScriptReadResult res = read_expr(ctx, OpPrecedence_Assignment);
  if (UNLIKELY(res.type != ScriptResult_Success)) {
    return res;
  }

  return read_success(script_add_var_store(ctx->doc, var->varSlot, res.expr));
}

static ScriptReadResult read_expr_var_modify(
    ScriptReadContext*    ctx,
    const StringHash      identifier,
    const ScriptTokenType type,
    const ScriptMarker    start) {
  const ScriptVarMeta* var = script_var_lookup(ctx, identifier);
  if (UNLIKELY(!var)) {
    return read_error(ctx, ScriptResult_NoVariableFoundForIdentifier, start);
  }

  const ScriptIntrinsic  intr = token_op_binary_modify(type);
  const ScriptReadResult val  = token_intr_rhs_scope(intr)
                                    ? read_expr_scope_single(ctx, OpPrecedence_Assignment)
                                    : read_expr(ctx, OpPrecedence_Assignment);
  if (UNLIKELY(val.type != ScriptResult_Success)) {
    return val;
  }

  const ScriptExpr loadExpr   = script_add_var_load(ctx->doc, var->varSlot);
  const ScriptExpr intrArgs[] = {loadExpr, val.expr};
  const ScriptExpr intrExpr   = script_add_intrinsic(ctx->doc, intr, intrArgs);
  return read_success(script_add_var_store(ctx->doc, var->varSlot, intrExpr));
}

static ScriptReadResult read_expr_mem_store(ScriptReadContext* ctx, const StringHash key) {
  const ScriptReadResult val = read_expr(ctx, OpPrecedence_Assignment);
  if (UNLIKELY(val.type != ScriptResult_Success)) {
    return val;
  }
  return read_success(script_add_mem_store(ctx->doc, key, val.expr));
}

static ScriptReadResult
read_expr_mem_modify(ScriptReadContext* ctx, const StringHash key, const ScriptTokenType type) {
  const ScriptIntrinsic  intr = token_op_binary_modify(type);
  const ScriptReadResult val  = token_intr_rhs_scope(intr)
                                    ? read_expr_scope_single(ctx, OpPrecedence_Assignment)
                                    : read_expr(ctx, OpPrecedence_Assignment);
  if (UNLIKELY(val.type != ScriptResult_Success)) {
    return val;
  }
  const ScriptExpr loadExpr   = script_add_mem_load(ctx->doc, key);
  const ScriptExpr intrArgs[] = {loadExpr, val.expr};
  const ScriptExpr intrExpr   = script_add_intrinsic(ctx->doc, intr, intrArgs);
  return read_success(script_add_mem_store(ctx->doc, key, intrExpr));
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptReadResult
read_expr_function(ScriptReadContext* ctx, const StringHash identifier, const ScriptMarker start) {
  ScriptExpr             args[script_args_max];
  const ScriptArgsResult argsRes = read_args(ctx, args);
  if (UNLIKELY(argsRes.type != ScriptResult_Success)) {
    return read_error(ctx, argsRes.type, start);
  }

  const ScriptBuiltinFunc* builtin = script_builtin_func_lookup(identifier, argsRes.argCount);
  if (builtin) {
    return read_success(script_add_intrinsic(ctx->doc, builtin->intr, args));
  }
  if (script_builtin_func_exists(identifier)) {
    return read_error(ctx, ScriptResult_IncorrectArgumentCountForBuiltinFunction, start);
  }

  if (ctx->binder) {
    const ScriptBinderSlot externFunc = script_binder_lookup(ctx->binder, identifier);
    if (!sentinel_check(externFunc)) {
      return read_success(script_add_extern(ctx->doc, externFunc, args, argsRes.argCount));
    }
  }

  return read_error(ctx, ScriptResult_NoFunctionFoundForIdentifier, start);
}

static ScriptReadResult read_expr_if(ScriptReadContext* ctx, const ScriptMarker start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_ParenOpen)) {
    return read_error(ctx, ScriptResult_InvalidConditionCount, start);
  }

  ScriptScope scope = {0};
  script_scope_push(ctx, &scope);

  ScriptExpr             conditions[script_args_max];
  const ScriptArgsResult conditionRes = read_args(ctx, conditions);
  if (UNLIKELY(conditionRes.type != ScriptResult_Success)) {
    return script_scope_pop(ctx), read_error(ctx, conditionRes.type, start);
  }
  if (UNLIKELY(conditionRes.argCount != 1)) {
    return script_scope_pop(ctx), read_error(ctx, ScriptResult_InvalidConditionCount, start);
  }

  if (!read_consume_if(ctx, ScriptTokenType_CurlyOpen)) {
    return script_scope_pop(ctx), read_error(ctx, ScriptResult_BlockExpected, start);
  }
  const ScriptReadResult b1 = read_expr_scope_block(ctx);
  if (UNLIKELY(b1.type != ScriptResult_Success)) {
    return script_scope_pop(ctx), b1;
  }

  ScriptExpr b2Expr;
  if (read_consume_if(ctx, ScriptTokenType_Else)) {
    const ScriptMarker startElse = script_marker(ctx);
    if (read_consume_if(ctx, ScriptTokenType_CurlyOpen)) {
      const ScriptReadResult b2 = read_expr_scope_block(ctx);
      if (UNLIKELY(b2.type != ScriptResult_Success)) {
        return script_scope_pop(ctx), b2;
      }
      b2Expr = b2.expr;
    } else if (read_consume_if(ctx, ScriptTokenType_If)) {
      const ScriptReadResult b2 = read_expr_if(ctx, startElse);
      if (UNLIKELY(b2.type != ScriptResult_Success)) {
        return script_scope_pop(ctx), b2;
      }
      b2Expr = b2.expr;
    } else {
      return script_scope_pop(ctx), read_error(ctx, ScriptResult_BlockOrIfExpected, startElse);
    }
  } else {
    b2Expr = script_add_value(ctx->doc, script_null());
  }

  diag_assert(&scope == script_scope_tail(ctx));
  script_scope_pop(ctx);

  const ScriptExpr intrArgs[] = {conditions[0], b1.expr, b2Expr};
  return read_success(script_add_intrinsic(ctx->doc, ScriptIntrinsic_If, intrArgs));
}

static ScriptReadResult read_expr_while(ScriptReadContext* ctx, const ScriptMarker start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_ParenOpen)) {
    return read_error(ctx, ScriptResult_InvalidWhileLoop, start);
  }

  ScriptScope scope = {0};
  script_scope_push(ctx, &scope);

  ScriptExpr             conditions[script_args_max];
  const ScriptArgsResult conditionRes = read_args(ctx, conditions);
  if (UNLIKELY(conditionRes.type != ScriptResult_Success)) {
    return script_scope_pop(ctx), read_error(ctx, conditionRes.type, start);
  }
  if (UNLIKELY(conditionRes.argCount != 1)) {
    return script_scope_pop(ctx), read_error(ctx, ScriptResult_InvalidWhileLoop, start);
  }

  if (!read_consume_if(ctx, ScriptTokenType_CurlyOpen)) {
    return script_scope_pop(ctx), read_error(ctx, ScriptResult_BlockExpected, start);
  }

  ctx->flags |= ScriptReadFlags_InsideLoop;
  const ScriptReadResult body = read_expr_scope_block(ctx);
  ctx->flags &= ~ScriptReadFlags_InsideLoop;

  if (UNLIKELY(body.type != ScriptResult_Success)) {
    return script_scope_pop(ctx), body;
  }

  diag_assert(&scope == script_scope_tail(ctx));
  script_scope_pop(ctx);

  const ScriptExpr intrArgs[] = {conditions[0], body.expr};
  return read_success(script_add_intrinsic(ctx->doc, ScriptIntrinsic_While, intrArgs));
}

typedef enum {
  ReadIfComp_Setup,
  ReadIfComp_Condition,
  ReadIfComp_Increment,
} ReadIfComp;

static ScriptReadResult read_expr_for_comp(ScriptReadContext* ctx, const ReadIfComp comp) {
  static const ScriptTokenType g_endTokens[] = {
      [ReadIfComp_Setup]     = ScriptTokenType_SemiColon,
      [ReadIfComp_Condition] = ScriptTokenType_SemiColon,
      [ReadIfComp_Increment] = ScriptTokenType_ParenClose,
  };
  const ScriptMarker start = script_marker(ctx);
  ScriptReadResult   res;
  if (read_peek(ctx).type == g_endTokens[comp]) {
    const ScriptVal skipVal = comp == ReadIfComp_Condition ? script_bool(true) : script_null();
    res                     = read_success(script_add_value(ctx->doc, skipVal));
  } else {
    res = read_expr(ctx, OpPrecedence_None);
    if (UNLIKELY(res.type != ScriptResult_Success)) {
      return res;
    }
  }
  if (!read_consume_if(ctx, g_endTokens[comp])) {
    return read_error(ctx, ScriptResult_InvalidForLoop, start);
  }
  return res;
}

static ScriptReadResult read_expr_for(ScriptReadContext* ctx, const ScriptMarker start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_ParenOpen)) {
    return read_error(ctx, ScriptResult_InvalidForLoop, start);
  }

  ScriptScope scope = {0};
  script_scope_push(ctx, &scope);

  const ScriptReadResult setupExprRes = read_expr_for_comp(ctx, ReadIfComp_Setup);
  if (UNLIKELY(setupExprRes.type != ScriptResult_Success)) {
    return script_scope_pop(ctx), setupExprRes;
  }
  const ScriptReadResult condExprRes = read_expr_for_comp(ctx, ReadIfComp_Condition);
  if (UNLIKELY(condExprRes.type != ScriptResult_Success)) {
    return script_scope_pop(ctx), condExprRes;
  }
  const ScriptReadResult incrExprRes = read_expr_for_comp(ctx, ReadIfComp_Increment);
  if (UNLIKELY(incrExprRes.type != ScriptResult_Success)) {
    return script_scope_pop(ctx), incrExprRes;
  }

  if (!read_consume_if(ctx, ScriptTokenType_CurlyOpen)) {
    return script_scope_pop(ctx), read_error(ctx, ScriptResult_BlockExpected, start);
  }

  ctx->flags |= ScriptReadFlags_InsideLoop;
  const ScriptReadResult body = read_expr_scope_block(ctx);
  ctx->flags &= ~ScriptReadFlags_InsideLoop;

  if (UNLIKELY(body.type != ScriptResult_Success)) {
    return script_scope_pop(ctx), body;
  }

  diag_assert(&scope == script_scope_tail(ctx));
  script_scope_pop(ctx);

  const ScriptExpr intrArgs[] = {setupExprRes.expr, condExprRes.expr, incrExprRes.expr, body.expr};
  return read_success(script_add_intrinsic(ctx->doc, ScriptIntrinsic_For, intrArgs));
}

static ScriptReadResult read_expr_select(ScriptReadContext* ctx, const ScriptExpr condition) {
  const ScriptMarker start = script_marker(ctx);

  const ScriptReadResult b1 = read_expr_scope_single(ctx, OpPrecedence_Conditional);
  if (UNLIKELY(b1.type != ScriptResult_Success)) {
    return b1;
  }

  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.type != ScriptTokenType_Colon)) {
    return read_error(ctx, ScriptResult_MissingColonInSelectExpression, start);
  }

  const ScriptReadResult b2 = read_expr_scope_single(ctx, OpPrecedence_Conditional);
  if (UNLIKELY(b2.type != ScriptResult_Success)) {
    return b2;
  }

  const ScriptExpr intrArgs[] = {condition, b1.expr, b2.expr};
  return read_success(script_add_intrinsic(ctx->doc, ScriptIntrinsic_Select, intrArgs));
}

static ScriptReadResult read_expr_primary(ScriptReadContext* ctx) {
  const ScriptMarker start = script_marker(ctx);

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
    return read_expr_if(ctx, start);
  case ScriptTokenType_While:
    return read_expr_while(ctx, start);
  case ScriptTokenType_For:
    return read_expr_for(ctx, start);
  case ScriptTokenType_Var:
    return read_expr_var_declare(ctx, start);
  case ScriptTokenType_Continue:
    if (!(ctx->flags & ScriptReadFlags_InsideLoop)) {
      return read_error(ctx, ScriptResult_NotValidOutsideLoopBody, start);
    }
    return read_success(script_add_intrinsic(ctx->doc, ScriptIntrinsic_Continue, null));
  case ScriptTokenType_Break:
    if (!(ctx->flags & ScriptReadFlags_InsideLoop)) {
      return read_error(ctx, ScriptResult_NotValidOutsideLoopBody, start);
    }
    return read_success(script_add_intrinsic(ctx->doc, ScriptIntrinsic_Break, null));
  /**
   * Identifiers.
   */
  case ScriptTokenType_Identifier: {
    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, null, &nextToken, ScriptLexFlags_None);
    switch (nextToken.type) {
    case ScriptTokenType_ParenOpen:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_function(ctx, token.val_identifier, start);
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
    const ScriptReadResult val = read_expr(ctx, OpPrecedence_Unary);
    if (UNLIKELY(val.type != ScriptResult_Success)) {
      return val;
    }
    const ScriptIntrinsic intr       = token_op_unary(token.type);
    const ScriptExpr      intrArgs[] = {val.expr};
    return read_success(script_add_intrinsic(ctx->doc, intr, intrArgs));
  }
  /**
   * Literals.
   */
  case ScriptTokenType_Number:
    return read_success(script_add_value(ctx->doc, script_number(token.val_number)));
  case ScriptTokenType_String:
    return read_success(script_add_value(ctx->doc, script_string(token.val_string)));
  /**
   * Memory access.
   */
  case ScriptTokenType_Key: {
    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, null, &nextToken, ScriptLexFlags_None);
    switch (nextToken.type) {
    case ScriptTokenType_Eq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_mem_store(ctx, token.val_key);
    case ScriptTokenType_PlusEq:
    case ScriptTokenType_MinusEq:
    case ScriptTokenType_StarEq:
    case ScriptTokenType_SlashEq:
    case ScriptTokenType_PercentEq:
    case ScriptTokenType_QMarkQMarkEq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_mem_modify(ctx, token.val_key, nextToken.type);
    default:
      return read_success(script_add_mem_load(ctx->doc, token.val_key));
    }
  }
  /**
   * Lex errors.
   */
  case ScriptTokenType_SemiColon:
    return read_error(ctx, ScriptResult_ExtraneousSemicolon, start);
  case ScriptTokenType_Error:
    return read_error(ctx, token.val_error, start);
  case ScriptTokenType_End:
    return read_error(ctx, ScriptResult_MissingPrimaryExpression, start);
  default:
    return read_error(ctx, ScriptResult_InvalidPrimaryExpression, start);
  }
}

static ScriptReadResult read_expr(ScriptReadContext* ctx, const OpPrecedence minPrecedence) {
  ++ctx->recursionDepth;
  if (UNLIKELY(ctx->recursionDepth >= script_depth_max)) {
    return read_error(ctx, ScriptResult_RecursionLimitExceeded, script_marker(ctx));
  }

  ScriptReadResult res = read_expr_primary(ctx);
  if (UNLIKELY(res.type != ScriptResult_Success)) {
    return res;
  }

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
      const ScriptReadResult selectExpr = read_expr_select(ctx, res.expr);
      if (UNLIKELY(selectExpr.type != ScriptResult_Success)) {
        return selectExpr;
      }
      res = read_success(selectExpr.expr);
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
      const ScriptIntrinsic  intr = token_op_binary(nextToken.type);
      const ScriptReadResult rhs  = token_intr_rhs_scope(intr)
                                        ? read_expr_scope_single(ctx, opPrecedence)
                                        : read_expr(ctx, opPrecedence);
      if (UNLIKELY(rhs.type != ScriptResult_Success)) {
        return rhs;
      }
      const ScriptExpr intrArgs[] = {res.expr, rhs.expr};
      res                         = read_success(script_add_intrinsic(ctx->doc, intr, intrArgs));
    } break;
    default:
      diag_assert_fail("Invalid operator token");
      UNREACHABLE
    }
  }
  --ctx->recursionDepth;
  return res;
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

void script_read(
    ScriptDoc* doc, const ScriptBinder* binder, const String str, ScriptReadResult* res) {
  script_read_init();

  if (binder) {
    script_link_binder(doc, binder);
  }

  ScriptScope       scopeRoot = {0};
  ScriptReadContext ctx       = {
            .doc        = doc,
            .binder     = binder,
            .input      = str,
            .inputTotal = str,
            .scopeRoot  = &scopeRoot,
  };
  script_var_free_all(&ctx);

  *res = read_expr_block(&ctx, ScriptBlockType_Implicit);

  if (res->type == ScriptResult_Success) {
    diag_assert_msg(read_peek(&ctx).type == ScriptTokenType_End, "Not all input consumed");
  }
}

void script_read_result_write(DynString* out, const ScriptDoc* doc, const ScriptReadResult* res) {
  if (res->type == ScriptResult_Success) {
    script_expr_str_write(doc, res->expr, 0, out);
  } else {
    fmt_write(
        out,
        "{}:{}-{}:{}: {}",
        fmt_int(res->errorStart.line),
        fmt_int(res->errorStart.column),
        fmt_int(res->errorEnd.line),
        fmt_int(res->errorEnd.column),
        fmt_text(script_result_str(res->type)));
  }
}

String script_read_result_scratch(const ScriptDoc* doc, const ScriptReadResult* res) {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_read_result_write(&buffer, doc, res);

  return dynstring_view(&buffer);
}
