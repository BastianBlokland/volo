#pragma once
#include "core_string.h"

typedef enum eScriptError {
  ScriptError_None,
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
  ScriptError_IncorrectArgumentCountForBuiltinFunction,
  ScriptError_UnclosedParenthesizedExpression,
  ScriptError_UnterminatedBlock,
  ScriptError_UnterminatedArgumentList,
  ScriptError_BlockSizeExceedsMaximum,
  ScriptError_MissingSemicolon,
  ScriptError_UnexpectedSemicolon,
  ScriptError_ArgumentCountExceedsMaximum,
  ScriptError_InvalidConditionCount,
  ScriptError_InvalidWhileLoop,
  ScriptError_InvalidForLoop,
  ScriptError_BlockExpected,
  ScriptError_BlockOrIfExpected,
  ScriptError_MissingColonInSelectExpression,
  ScriptError_UnexpectedTokenAfterExpression,
  ScriptError_NotValidOutsideLoopBody,
  ScriptError_VariableDeclareNotAllowed,
  ScriptError_VariableUnused,
  ScriptError_AssertionFailed,
  ScriptError_LoopInterationLimitExceeded,

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
