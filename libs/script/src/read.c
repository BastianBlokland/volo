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

static ScriptOpBin op_bin(const ScriptTokenType type) {
  switch (type) {
  case ScriptTokenType_OpEqEq:
    return ScriptOpBin_Equal;
  case ScriptTokenType_OpBangEq:
    return ScriptOpBin_NotEqual;
  case ScriptTokenType_OpLe:
    return ScriptOpBin_Less;
  case ScriptTokenType_OpLeEq:
    return ScriptOpBin_LessOrEqual;
  case ScriptTokenType_OpGt:
    return ScriptOpBin_Greater;
  case ScriptTokenType_OpGtEq:
    return ScriptOpBin_GreaterOrEqual;
  case ScriptTokenType_OpPlus:
    return ScriptOpBin_Add;
  case ScriptTokenType_OpMinus:
    return ScriptOpBin_Sub;
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
  case ScriptTokenType_SepParenOpen:
    return read_expr_paren(ctx);
  case ScriptTokenType_LitNull:
    return script_expr(script_add_value(ctx->doc, script_null()));
  case ScriptTokenType_LitNumber:
    return script_expr(script_add_value(ctx->doc, script_number(token.val_number)));
  case ScriptTokenType_LitBool:
    return script_expr(script_add_value(ctx->doc, script_bool(token.val_bool)));
  case ScriptTokenType_LitKey:
    return script_expr(script_add_load(ctx->doc, token.val_key));
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
      res = script_expr(script_add_op_bin(ctx->doc, res.expr, rhs.expr, op_bin(nextToken.type)));
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
