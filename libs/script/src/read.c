#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "script_lex.h"
#include "script_read.h"

#include "doc_internal.h"

#define script_depth_max 25
#define script_args_max 10

#define script_err(_ERR_)                                                                          \
  (ScriptReadResult) { .type = ScriptResult_Fail, .error = (_ERR_) }

#define script_expr(_EXPR_)                                                                        \
  (ScriptReadResult) { .type = ScriptResult_Success, .expr = (_EXPR_) }

typedef struct {
  String     name;
  StringHash nameHash; // NOTE: Initialized at runtime.
  u32        argCount;
  u32        opType; // ScriptOpUnary / ScriptOpBinary
} ScriptFunction;

static ScriptFunction g_scriptReadFuncs[] = {
    {.name = string_static("distance"), .argCount = 2, .opType = ScriptOpBinary_Distance},
};

typedef enum {
  OpPrecedence_None,
  OpPrecedence_Grouping,
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
    return OpPrecedence_Multiplicative;
  case ScriptTokenType_AmpAmp:
  case ScriptTokenType_PipePipe:
    return OpPrecedence_Logical;
  case ScriptTokenType_QMarkQMark:
    return OpPrecedence_Conditional;
  case ScriptTokenType_SemiColon:
    return OpPrecedence_Grouping;
  default:
    return OpPrecedence_None;
  }
}

static ScriptOpUnary token_op_unary(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_Minus:
    return ScriptOpUnary_Negate;
  case ScriptTokenType_Bang:
    return ScriptOpUnary_Invert;
  default:
    diag_assert_fail("Invalid unary operation token");
    UNREACHABLE
  }
}

static ScriptOpBinary token_op_binary(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_EqEq:
    return ScriptOpBinary_Equal;
  case ScriptTokenType_BangEq:
    return ScriptOpBinary_NotEqual;
  case ScriptTokenType_Le:
    return ScriptOpBinary_Less;
  case ScriptTokenType_LeEq:
    return ScriptOpBinary_LessOrEqual;
  case ScriptTokenType_Gt:
    return ScriptOpBinary_Greater;
  case ScriptTokenType_GtEq:
    return ScriptOpBinary_GreaterOrEqual;
  case ScriptTokenType_Plus:
    return ScriptOpBinary_Add;
  case ScriptTokenType_Minus:
    return ScriptOpBinary_Sub;
  case ScriptTokenType_Star:
    return ScriptOpBinary_Mul;
  case ScriptTokenType_Slash:
    return ScriptOpBinary_Div;
  case ScriptTokenType_SemiColon:
    return ScriptOpBinary_RetRight;
  case ScriptTokenType_AmpAmp:
    return ScriptOpBinary_LogicAnd;
  case ScriptTokenType_PipePipe:
    return ScriptOpBinary_LogicOr;
  case ScriptTokenType_QMarkQMark:
    return ScriptOpBinary_NullCoalescing;
  default:
    diag_assert_fail("Invalid binary operation token");
    UNREACHABLE
  }
}

typedef struct {
  ScriptDoc* doc;
  String     input;
  u32        recursionDepth;
} ScriptReadContext;

static bool read_at_end(const ScriptReadContext* ctx) {
  ScriptToken token;
  script_lex(ctx->input, null, &token);
  return token.type == ScriptTokenType_End;
}

static ScriptReadResult read_expr(ScriptReadContext*, OpPrecedence minPrecedence);

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
  if (token.type == ScriptTokenType_ParenClose) {
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

static ScriptReadResult read_expr_constant(ScriptReadContext* ctx, const StringHash identifier) {
  const ScriptValId constValId = script_doc_constant_lookup(ctx->doc, identifier);
  if (LIKELY(!sentinel_check(constValId))) {
    return script_expr(script_add_value_id(ctx->doc, constValId));
  }
  return script_err(ScriptError_NoConstantFoundForIdentifier);
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptReadResult read_expr_function(ScriptReadContext* ctx, const StringHash identifier) {
  ScriptExpr             args[script_args_max];
  const ScriptArgsResult argsRes = read_args(ctx, args);
  if (argsRes.type == ScriptResult_Fail) {
    return script_err(argsRes.error);
  }

  array_for_t(g_scriptReadFuncs, ScriptFunction, func) {
    if (func->nameHash == identifier && argsRes.argCount == func->argCount) {
      switch (func->argCount) {
      case 1:
        return script_expr(script_add_op_unary(ctx->doc, args[0], func->opType));
      case 2:
        return script_expr(script_add_op_binary(ctx->doc, args[0], args[1], func->opType));
      default:
        diag_crash_msg("Unsupported function argument count ({})", fmt_int(func->argCount));
      }
    }
  }
  return script_err(ScriptError_NoFunctionFoundForIdentifier);
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
     * Identifiers.
     */
  case ScriptTokenType_Identifier: {
    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, null, &nextToken);
    if (nextToken.type == ScriptTokenType_ParenOpen) {
      ctx->input = remInput; // Consume the opening parenthesis.
      return read_expr_function(ctx, token.val_identifier);
    }
    return read_expr_constant(ctx, token.val_identifier);
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
    const ScriptOpUnary op = token_op_unary(token.type);
    return script_expr(script_add_op_unary(ctx->doc, val.expr, op));
  }
  /**
   * Literals.
   */
  case ScriptTokenType_Number:
    return script_expr(script_add_value(ctx->doc, script_number(token.val_number)));
  /**
   * Memory access.
   */
  case ScriptTokenType_Key: {
    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, null, &nextToken);
    if (nextToken.type == ScriptTokenType_Eq) {
      ctx->input                 = remInput; // Consume the 'nextToken'.
      const ScriptReadResult val = read_expr(ctx, OpPrecedence_Assignment);
      if (UNLIKELY(val.type == ScriptResult_Fail)) {
        return val;
      }
      return script_expr(script_add_store(ctx->doc, token.val_key, val.expr));
    }
    return script_expr(script_add_load(ctx->doc, token.val_key));
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
     * Binary expressions.
     */
    switch (nextToken.type) {
    case ScriptTokenType_SemiColon:
      // Expressions are allowed to be ended with semi-colons.
      if (read_at_end(ctx)) {
        ctx->input = string_empty;
        return res;
      }
      // Fallthrough.
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
    case ScriptTokenType_AmpAmp:
    case ScriptTokenType_PipePipe:
    case ScriptTokenType_QMarkQMark: {
      const ScriptReadResult rhs = read_expr(ctx, opPrecedence);
      if (UNLIKELY(rhs.type == ScriptResult_Fail)) {
        return rhs;
      }
      const ScriptOpBinary op = token_op_binary(nextToken.type);
      res                     = script_expr(script_add_op_binary(ctx->doc, res.expr, rhs.expr, op));
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
    array_for_t(g_scriptReadFuncs, ScriptFunction, func) {
      func->nameHash = string_hash(func->name);
    }
    g_init = true;
  }
  thread_spinlock_unlock(&g_initLock);
}

String script_read_expr(ScriptDoc* doc, const String str, ScriptReadResult* res) {
  script_read_init();

  ScriptReadContext ctx = {
      .doc   = doc,
      .input = str,
  };
  *res = read_expr(&ctx, OpPrecedence_None);
  return ctx.input;
}

void script_read_all(ScriptDoc* doc, const String str, ScriptReadResult* res) {
  script_read_init();

  ScriptReadContext ctx = {
      .doc   = doc,
      .input = str,
  };
  *res = read_expr(&ctx, OpPrecedence_None);

  ScriptToken token;
  script_lex(ctx.input, null, &token);
  if (UNLIKELY(token.type != ScriptTokenType_End)) {
    *res = script_err(ScriptError_UnexpectedTokenAfterExpression);
  }
}
