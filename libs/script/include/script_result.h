#pragma once
#include "core_string.h"

typedef enum eScriptResult {
  ScriptResult_Success,
  ScriptResult_InvalidChar,
  ScriptResult_InvalidUtf8,
  ScriptResult_KeyEmpty,
  ScriptResult_UnterminatedString,
  ScriptResult_RecursionLimitExceeded,
  ScriptResult_VariableLimitExceeded,
  ScriptResult_VariableIdentifierMissing,
  ScriptResult_VariableIdentifierConflicts,
  ScriptResult_MissingPrimaryExpression,
  ScriptResult_InvalidPrimaryExpression,
  ScriptResult_NoVariableFoundForIdentifier,
  ScriptResult_NoFunctionFoundForIdentifier,
  ScriptResult_IncorrectArgumentCountForBuiltinFunction,
  ScriptResult_UnclosedParenthesizedExpression,
  ScriptResult_UnterminatedBlock,
  ScriptResult_UnterminatedArgumentList,
  ScriptResult_BlockSizeExceedsMaximum,
  ScriptResult_MissingSemicolon,
  ScriptResult_ExtraneousSemicolon,
  ScriptResult_ArgumentCountExceedsMaximum,
  ScriptResult_InvalidConditionCount,
  ScriptResult_InvalidWhileLoop,
  ScriptResult_InvalidForLoop,
  ScriptResult_BlockExpected,
  ScriptResult_BlockOrIfExpected,
  ScriptResult_MissingColonInSelectExpression,
  ScriptResult_UnexpectedTokenAfterExpression,
  ScriptResult_NotValidOutsideLoopBody,
  ScriptResult_AssertionFailed,
  ScriptResult_LoopInterationLimitExceeded,

  ScriptResult_Count,
} ScriptResult;

/**
 * Return a textual representation of the given ScriptResult.
 */
String script_result_str(ScriptResult);

/**
 * Create a formatting argument for a script result.
 */
#define script_result_fmt(_VAL_) fmt_text(script_result_str(_VAL_))
