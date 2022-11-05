#pragma once
#include "core_string.h"

typedef enum {
  ScriptError_InvalidChar,
  ScriptError_InvalidUtf8,
  ScriptError_KeyEmpty,
  ScriptError_RecursionLimitExceeded,
  ScriptError_MissingPrimaryExpression,
  ScriptError_InvalidPrimaryExpression,
  ScriptError_NoConstantFoundForIdentifier,
  ScriptError_NoFunctionFoundForIdentifier,
  ScriptError_UnclosedParenthesizedExpression,
  ScriptError_UnterminatedArgumentList,
  ScriptError_ArgumentCountExceedsMaximum,
  ScriptError_MissingColonInSelectExpression,
  ScriptError_UnexpectedTokenAfterExpression,

  ScriptError_Count,
} ScriptError;

/**
 * Return a textual representation of the given ScriptError.
 */
String script_error_str(ScriptError);

/**
 * Create a formatting argument for a script error.
 */
#define script_error_fmt(_VAL_) fmt_text(script_error_str(_VAL_))
