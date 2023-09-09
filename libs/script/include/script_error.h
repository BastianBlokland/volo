#pragma once
#include "core_string.h"

typedef enum {
  ScriptError_InvalidChar,
  ScriptError_InvalidUtf8,
  ScriptError_KeyEmpty,
  ScriptError_UnterminatedString,
  ScriptError_RecursionLimitExceeded,
  ScriptError_VariableLimitExceeded,
  ScriptError_VariableIdentifierMissing,
  ScriptError_VariableIdentifierConflicts,
  ScriptError_MissingPrimaryExpression,
  ScriptError_InvalidPrimaryExpression,
  ScriptError_NoVariableFoundForIdentifier,
  ScriptError_NoFunctionFoundForIdentifier,
  ScriptError_UnclosedParenthesizedExpression,
  ScriptError_UnterminatedScope,
  ScriptError_UnterminatedArgumentList,
  ScriptError_BlockSizeExceedsMaximum,
  ScriptError_ArgumentCountExceedsMaximum,
  ScriptError_InvalidConditionCountForIf,
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
