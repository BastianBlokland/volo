#include "core_diag.h"
#include "script_lex.h"
#include "script_read.h"

#define script_depth_max 25

#define script_err(_ERR_)                                                                          \
  (ScriptReadResult) { .type = ScriptResult_Fail, .error = (_ERR_) }

#define script_expr(_EXPR_)                                                                        \
  (ScriptReadResult) { .type = ScriptResult_Success, .expr = (_EXPR_) }

typedef enum {
  OpPrecedence_None,
  OpPrecedence_Equality,
  OpPrecedence_Relational,
  OpPrecedence_Additive,
  OpPrecedence_Unary,
} OpPrecedence;

static OpPrecedence op_precedence(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_OpEqEq:
  case ScriptTokenType_OpBangEq:
    return OpPrecedence_Equality;
  case ScriptTokenType_OpLe:
  case ScriptTokenType_OpLeEq:
  case ScriptTokenType_OpGt:
  case ScriptTokenType_OpGtEq:
    return OpPrecedence_Relational;
  case ScriptTokenType_OpPlus:
  case ScriptTokenType_OpMinus:
    return OpPrecedence_Additive;
  default:
    return OpPrecedence_None;
  }
}

static ScriptOpUnary token_op_unary(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_OpMinus:
    return ScriptOpUnary_Negate;
  default:
    diag_assert_fail("Invalid unary operation token");
    UNREACHABLE
  }
}

static ScriptOpBinary token_op_binary(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_OpEqEq:
    return ScriptOpBinary_Equal;
  case ScriptTokenType_OpBangEq:
    return ScriptOpBinary_NotEqual;
  case ScriptTokenType_OpLe:
    return ScriptOpBinary_Less;
  case ScriptTokenType_OpLeEq:
    return ScriptOpBinary_LessOrEqual;
  case ScriptTokenType_OpGt:
    return ScriptOpBinary_Greater;
  case ScriptTokenType_OpGtEq:
    return ScriptOpBinary_GreaterOrEqual;
  case ScriptTokenType_OpPlus:
    return ScriptOpBinary_Add;
  case ScriptTokenType_OpMinus:
    return ScriptOpBinary_Sub;
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

static ScriptReadResult read_expr(ScriptReadContext*, OpPrecedence minPrecedence);

static ScriptReadResult read_expr_paren(ScriptReadContext* ctx) {
  const ScriptReadResult res = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(res.type == ScriptResult_Fail)) {
    return res;
  }
  ScriptToken closeToken;
  ctx->input = script_lex(ctx->input, null, &closeToken);
  if (UNLIKELY(closeToken.type != ScriptTokenType_SepParenClose)) {
    return script_err(ScriptError_UnclosedParenthesizedExpression);
  }
  return res;
}

static ScriptReadResult read_expr_primary(ScriptReadContext* ctx) {
  ScriptToken token;
  ctx->input = script_lex(ctx->input, g_stringtable, &token);

  switch (token.type) {
  /**
   * Parenthesized expression.
   */
  case ScriptTokenType_SepParenOpen:
    return read_expr_paren(ctx);
  /**
   * Unary operators.
   */
  case ScriptTokenType_OpMinus: {
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
  case ScriptTokenType_LitNull:
    return script_expr(script_add_value(ctx->doc, script_null()));
  case ScriptTokenType_LitNumber:
    return script_expr(script_add_value(ctx->doc, script_number(token.val_number)));
  case ScriptTokenType_LitBool:
    return script_expr(script_add_value(ctx->doc, script_bool(token.val_bool)));
  case ScriptTokenType_LitKey:
    return script_expr(script_add_load(ctx->doc, token.val_key));
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
    case ScriptTokenType_OpEqEq:
    case ScriptTokenType_OpBangEq:
    case ScriptTokenType_OpLe:
    case ScriptTokenType_OpLeEq:
    case ScriptTokenType_OpGt:
    case ScriptTokenType_OpGtEq:
    case ScriptTokenType_OpPlus:
    case ScriptTokenType_OpMinus: {
      const ScriptReadResult rhs = read_expr(ctx, opPrecedence);
      if (UNLIKELY(rhs.type == ScriptResult_Fail)) {
        return res;
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

String script_read_expr(ScriptDoc* doc, const String str, ScriptReadResult* res) {
  ScriptReadContext ctx = {
      .doc   = doc,
      .input = str,
  };
  *res = read_expr(&ctx, OpPrecedence_None);
  return ctx.input;
}

void script_read_all(ScriptDoc* doc, const String str, ScriptReadResult* res) {
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
