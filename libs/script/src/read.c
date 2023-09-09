#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "script_lex.h"
#include "script_read.h"

#include "doc_internal.h"

#define script_depth_max 25
#define script_block_size_max 128
#define script_args_max 10
#define script_builtin_consts_max 32
#define script_builtin_funcs_max 32

#define script_err(_ERR_)                                                                          \
  (ScriptReadResult) { .type = ScriptResult_Fail, .error = (_ERR_) }

#define script_expr(_EXPR_)                                                                        \
  (ScriptReadResult) { .type = ScriptResult_Success, .expr = (_EXPR_) }

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

  // Builtin functions.
  script_builtin_func_add(string_lit("vector"), ScriptIntrinsic_ComposeVector3);
  script_builtin_func_add(string_lit("vector_x"), ScriptIntrinsic_VectorX);
  script_builtin_func_add(string_lit("vector_y"), ScriptIntrinsic_VectorY);
  script_builtin_func_add(string_lit("vector_z"), ScriptIntrinsic_VectorZ);
  script_builtin_func_add(string_lit("distance"), ScriptIntrinsic_Distance);
  script_builtin_func_add(string_lit("distance"), ScriptIntrinsic_Magnitude);
  script_builtin_func_add(string_lit("normalize"), ScriptIntrinsic_Normalize);
  script_builtin_func_add(string_lit("angle"), ScriptIntrinsic_Angle);
  script_builtin_func_add(string_lit("random"), ScriptIntrinsic_Random);
  script_builtin_func_add(string_lit("random"), ScriptIntrinsic_RandomBetween);
  script_builtin_func_add(string_lit("round_down"), ScriptIntrinsic_RoundDown);
  script_builtin_func_add(string_lit("round_nearest"), ScriptIntrinsic_RoundNearest);
  script_builtin_func_add(string_lit("round_up"), ScriptIntrinsic_RoundUp);
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

typedef struct {
  ScriptDoc* doc;
  String     input;
  u32        recursionDepth;
} ScriptReadContext;

static ScriptReadResult read_expr(ScriptReadContext*, OpPrecedence minPrecedence);

typedef enum {
  ScriptBlockType_Root,
  ScriptBlockType_Scope,
} ScriptBlockType;

/**
 * NOTE: For scope block the caller is expected to consume the opening curly brace.
 */
static ScriptReadResult read_expr_block(ScriptReadContext* ctx, const ScriptBlockType type) {
  ScriptToken token;
  ScriptExpr  exprs[script_block_size_max];
  u32         exprCount = 0;

  script_lex(ctx->input, null, &token);
  if (token.type == ScriptTokenType_CurlyClose || token.type == ScriptTokenType_End) {
    // Empty scope.
    goto BlockEnd;
  }

BlockNext:
  if (UNLIKELY(exprCount == script_block_size_max)) {
    return script_err(ScriptError_BlockSizeExceedsMaximum);
  }
  const ScriptReadResult arg = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(arg.type == ScriptResult_Fail)) {
    return script_err(arg.error);
  }
  exprs[exprCount++] = arg.expr;

  const String remInput = script_lex(ctx->input, null, &token);
  if (token.type == ScriptTokenType_SemiColon) {
    ctx->input = remInput; // Consume the semi.

    script_lex(ctx->input, null, &token);
    if (token.type == ScriptTokenType_End) {
      ctx->input = string_empty;
      goto BlockEnd;
    }
    if (type == ScriptBlockType_Scope && token.type == ScriptTokenType_CurlyClose) {
      goto BlockEnd;
    }
    goto BlockNext;
  }

BlockEnd:
  if (type == ScriptBlockType_Scope) {
    ctx->input = script_lex(ctx->input, null, &token);
    if (UNLIKELY(token.type != ScriptTokenType_CurlyClose)) {
      return script_err(ScriptError_UnterminatedScope);
    }
  }
  switch (exprCount) {
  case 0:
    return script_expr(script_add_value(ctx->doc, script_null()));
  case 1:
    return script_expr(exprs[0]);
  default:
    return script_expr(script_add_block(ctx->doc, exprs, exprCount));
  }
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptReadResult read_expr_paren(ScriptReadContext* ctx) {
  const ScriptReadResult res = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(res.type == ScriptResult_Fail)) {
    return res;
  }
  ScriptToken closeToken;
  ctx->input = script_lex(ctx->input, null, &closeToken);
  if (UNLIKELY(closeToken.type != ScriptTokenType_ParenClose)) {
    return script_err(ScriptError_UnclosedParenthesizedExpression);
  }
  return res;
}

typedef struct {
  ScriptResult type;
  union {
    u32         argCount;
    ScriptError error;
  };
} ScriptArgsResult;

#define script_args_success(_COUNT_)                                                               \
  (ScriptArgsResult) { .type = ScriptResult_Success, .argCount = (_COUNT_) }

#define script_args_err(_ERR_)                                                                     \
  (ScriptArgsResult) { .type = ScriptResult_Fail, .error = (_ERR_) }

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptArgsResult read_args(ScriptReadContext* ctx, ScriptExpr out[script_args_max]) {
  ScriptToken token;
  u32         count = 0;

  script_lex(ctx->input, null, &token);
  if (token.type == ScriptTokenType_ParenClose || token.type == ScriptTokenType_End) {
    // Empty argument list.
    goto ArgEnd;
  }

ArgNext:
  if (UNLIKELY(count == script_args_max)) {
    return script_args_err(ScriptError_ArgumentCountExceedsMaximum);
  }
  const ScriptReadResult arg = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(arg.type == ScriptResult_Fail)) {
    return script_args_err(arg.error);
  }
  out[count++] = arg.expr;

  const String remInput = script_lex(ctx->input, null, &token);
  if (token.type == ScriptTokenType_Comma) {
    ctx->input = remInput; // Consume the comma.
    goto ArgNext;
  }

ArgEnd:
  ctx->input = script_lex(ctx->input, null, &token);
  if (UNLIKELY(token.type != ScriptTokenType_ParenClose)) {
    return script_args_err(ScriptError_UnterminatedArgumentList);
  }
  return script_args_success(count);
}

static ScriptReadResult read_expr_var(ScriptReadContext* ctx, const StringHash identifier) {
  const ScriptBuiltinConst* builtin = script_builtin_const_lookup(identifier);
  if (builtin) {
    return script_expr(script_add_value(ctx->doc, builtin->val));
  }
  return script_err(ScriptError_NoVariableFoundForIdentifier);
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptReadResult read_expr_function(ScriptReadContext* ctx, const StringHash identifier) {
  ScriptExpr             args[script_args_max];
  const ScriptArgsResult argsRes = read_args(ctx, args);
  if (UNLIKELY(argsRes.type == ScriptResult_Fail)) {
    return script_err(argsRes.error);
  }

  const ScriptBuiltinFunc* builtin = script_builtin_func_lookup(identifier, argsRes.argCount);
  if (builtin) {
    return script_expr(script_add_intrinsic(ctx->doc, builtin->intr, args));
  }

  return script_err(ScriptError_NoFunctionFoundForIdentifier);
}

static ScriptReadResult read_expr_if(ScriptReadContext* ctx) {
  ScriptToken token;
  ctx->input = script_lex(ctx->input, g_stringtable, &token);
  if (UNLIKELY(token.type != ScriptTokenType_ParenOpen)) {
    return script_err(ScriptError_InvalidConditionCountForIf);
  }

  ScriptExpr             conditions[script_args_max];
  const ScriptArgsResult conditionRes = read_args(ctx, conditions);
  if (UNLIKELY(conditionRes.type == ScriptResult_Fail)) {
    return script_err(conditionRes.error);
  }
  if (UNLIKELY(conditionRes.argCount != 1)) {
    return script_err(ScriptError_InvalidConditionCountForIf);
  }

  const ScriptReadResult b1 = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(b1.type == ScriptResult_Fail)) {
    return b1;
  }

  ScriptExpr   b2Expr;
  const String remInput = script_lex(ctx->input, null, &token);
  if (token.type == ScriptTokenType_Else) {
    ctx->input = remInput; // Consume the else keyword.

    const ScriptReadResult b2 = read_expr(ctx, OpPrecedence_None);
    if (UNLIKELY(b2.type == ScriptResult_Fail)) {
      return b2;
    }
    b2Expr = b2.expr;
  } else {
    b2Expr = script_add_value(ctx->doc, script_null());
  }

  const ScriptExpr intrArgs[] = {conditions[0], b1.expr, b2Expr};
  return script_expr(script_add_intrinsic(ctx->doc, ScriptIntrinsic_If, intrArgs));
}

static ScriptReadResult read_expr_select(ScriptReadContext* ctx, const ScriptExpr condition) {
  const ScriptReadResult b1 = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(b1.type == ScriptResult_Fail)) {
    return b1;
  }

  ScriptToken token;
  ctx->input = script_lex(ctx->input, g_stringtable, &token);
  if (UNLIKELY(token.type != ScriptTokenType_Colon)) {
    return script_err(ScriptError_MissingColonInSelectExpression);
  }

  const ScriptReadResult b2 = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(b2.type == ScriptResult_Fail)) {
    return b2;
  }

  const ScriptExpr intrArgs[] = {condition, b1.expr, b2.expr};
  return script_expr(script_add_intrinsic(ctx->doc, ScriptIntrinsic_If, intrArgs));
}

static ScriptReadResult read_expr_primary(ScriptReadContext* ctx) {
  ScriptToken token;
  ctx->input = script_lex(ctx->input, g_stringtable, &token);

  switch (token.type) {
  /**
   * Parenthesized expression.
   */
  case ScriptTokenType_ParenOpen:
    return read_expr_paren(ctx);
  /**
   * Scope.
   */
  case ScriptTokenType_CurlyOpen:
    return read_expr_block(ctx, ScriptBlockType_Scope);
  /**
   * Keywords.
   */
  case ScriptTokenType_If:
    return read_expr_if(ctx);
  /**
   * Identifiers.
   */
  case ScriptTokenType_Identifier: {
    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, null, &nextToken);
    if (nextToken.type == ScriptTokenType_ParenOpen) {
      ctx->input = remInput; // Consume the opening parenthesis.
      return read_expr_function(ctx, token.val_identifier);
    }
    return read_expr_var(ctx, token.val_identifier);
  }
  /**
   * Unary operators.
   */
  case ScriptTokenType_Minus:
  case ScriptTokenType_Bang: {
    const ScriptReadResult val = read_expr(ctx, OpPrecedence_Unary);
    if (UNLIKELY(val.type == ScriptResult_Fail)) {
      return val;
    }
    const ScriptIntrinsic intr       = token_op_unary(token.type);
    const ScriptExpr      intrArgs[] = {val.expr};
    return script_expr(script_add_intrinsic(ctx->doc, intr, intrArgs));
  }
  /**
   * Literals.
   */
  case ScriptTokenType_Number:
    return script_expr(script_add_value(ctx->doc, script_number(token.val_number)));
  case ScriptTokenType_String:
    return script_expr(script_add_value(ctx->doc, script_string(token.val_string)));
  /**
   * Memory access.
   */
  case ScriptTokenType_Key: {
    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, null, &nextToken);
    switch (nextToken.type) {
    case ScriptTokenType_Eq: {
      ctx->input                 = remInput; // Consume the 'nextToken'.
      const ScriptReadResult val = read_expr(ctx, OpPrecedence_Assignment);
      if (UNLIKELY(val.type == ScriptResult_Fail)) {
        return val;
      }
      return script_expr(script_add_mem_store(ctx->doc, token.val_key, val.expr));
    }
    case ScriptTokenType_PlusEq:
    case ScriptTokenType_MinusEq:
    case ScriptTokenType_StarEq:
    case ScriptTokenType_SlashEq:
    case ScriptTokenType_PercentEq:
    case ScriptTokenType_QMarkQMarkEq: {
      ctx->input                 = remInput; // Consume the 'nextToken'.
      const ScriptReadResult val = read_expr(ctx, OpPrecedence_Assignment);
      if (UNLIKELY(val.type == ScriptResult_Fail)) {
        return val;
      }
      const ScriptExpr      loadExpr   = script_add_mem_load(ctx->doc, token.val_key);
      const ScriptIntrinsic itr        = token_op_binary_modify(nextToken.type);
      const ScriptExpr      intrArgs[] = {loadExpr, val.expr};
      const ScriptExpr      itrExpr    = script_add_intrinsic(ctx->doc, itr, intrArgs);
      return script_expr(script_add_mem_store(ctx->doc, token.val_key, itrExpr));
    }
    default:
      return script_expr(script_add_mem_load(ctx->doc, token.val_key));
    }
  }
  /**
   * Lex errors.
   */
  case ScriptTokenType_Error:
    return script_err(token.val_error);
  case ScriptTokenType_End:
    return script_err(ScriptError_MissingPrimaryExpression);
  default:
    return script_err(ScriptError_InvalidPrimaryExpression);
  }
}

static ScriptReadResult read_expr(ScriptReadContext* ctx, const OpPrecedence minPrecedence) {
  ++ctx->recursionDepth;
  if (UNLIKELY(ctx->recursionDepth >= script_depth_max)) {
    return script_err(ScriptError_RecursionLimitExceeded);
  }

  ScriptReadResult res = read_expr_primary(ctx);
  if (UNLIKELY(res.type == ScriptResult_Fail)) {
    return res;
  }

  /**
   * Test if the next token is an operator with higher precedence.
   */
  while (true) {
    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, g_stringtable, &nextToken);

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
      if (UNLIKELY(selectExpr.type == ScriptResult_Fail)) {
        return selectExpr;
      }
      res = script_expr(selectExpr.expr);
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
      const ScriptReadResult rhs = read_expr(ctx, opPrecedence);
      if (UNLIKELY(rhs.type == ScriptResult_Fail)) {
        return rhs;
      }
      const ScriptIntrinsic intr       = token_op_binary(nextToken.type);
      const ScriptExpr      intrArgs[] = {res.expr, rhs.expr};
      res = script_expr(script_add_intrinsic(ctx->doc, intr, intrArgs));
    } break;
    default:
      diag_assert_fail("Invalid operator token");
      UNREACHABLE
    }
  }
  --ctx->recursionDepth;
  return res;
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

void script_read(ScriptDoc* doc, const String str, ScriptReadResult* res) {
  script_read_init();

  ScriptReadContext ctx = {
      .doc   = doc,
      .input = str,
  };
  *res = read_expr_block(&ctx, ScriptBlockType_Root);

  ScriptToken token;
  script_lex(ctx.input, null, &token);
  if (UNLIKELY(res->type == ScriptResult_Success && token.type != ScriptTokenType_End)) {
    *res = script_err(ScriptError_UnexpectedTokenAfterExpression);
  }
}
