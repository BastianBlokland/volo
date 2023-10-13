#pragma once
#include "core_string.h"

typedef enum eScriptError {
  ScriptError_None,
  ScriptError_InvalidChar,
  ScriptError_InvalidUtf8,
  ScriptError_InvalidCharInNumber,
  ScriptError_NumberEndsWithDecPoint,
  ScriptError_NumberEndsWithSeparator,
  ScriptError_KeyEmpty,
  ScriptError_UnterminatedString,
  ScriptError_RecursionLimitExceeded,
  ScriptError_VarLimitExceeded,
  ScriptError_VarIdInvalid,
  ScriptError_VarIdConflicts,
  ScriptError_MissingPrimaryExpr,
  ScriptError_InvalidPrimaryExpr,
  ScriptError_NoVarFoundForId,
  ScriptError_NoFuncFoundForId,
  ScriptError_IncorrectArgCountForBuiltinFunc,
  ScriptError_UnclosedParenthesizedExpr,
  ScriptError_UnterminatedBlock,
  ScriptError_UnterminatedArgumentList,
  ScriptError_BlockTooBig,
  ScriptError_MissingSemicolon,
  ScriptError_UnexpectedSemicolon,
  ScriptError_UnnecessarySemicolon,
  ScriptError_ArgumentCountExceedsMaximum,
  ScriptError_InvalidConditionCount,
  ScriptError_InvalidIf,
  ScriptError_InvalidWhileLoop,
  ScriptError_InvalidForLoop,
  ScriptError_ForLoopCompMissing,
  ScriptError_ForLoopSeparatorMissing,
  ScriptError_BlockExpected,
  ScriptError_BlockOrIfExpected,
  ScriptError_MissingColonInSelectExpr,
  ScriptError_UnexpectedTokenAfterExpr,
  ScriptError_NotValidOutsideLoop,
  ScriptError_VarDeclareNotAllowed,
  ScriptError_VarUnused,
  ScriptError_LoopNotAllowed,
  ScriptError_IfNotAllowed,
  ScriptError_ReturnNotAllowed,
  ScriptError_ExprHasNoEffect,
  ScriptError_ExprUnreachable,
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
