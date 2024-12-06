#pragma once
#include "script_pos.h"

#define script_diag_max 16

typedef enum {
  ScriptDiag_InvalidChar,
  ScriptDiag_InvalidUtf8,
  ScriptDiag_InvalidCharInNumber,
  ScriptDiag_NumberEndsWithDecPoint,
  ScriptDiag_NumberEndsWithSeparator,
  ScriptDiag_KeyEmpty,
  ScriptDiag_UnterminatedString,
  ScriptDiag_UnexpectedWhitespace,
  ScriptDiag_RecursionLimitExceeded,
  ScriptDiag_VarLimitExceeded,
  ScriptDiag_VarIdInvalid,
  ScriptDiag_VarIdConflicts,
  ScriptDiag_MissingPrimaryExpr,
  ScriptDiag_InvalidPrimaryExpr,
  ScriptDiag_NoVarFoundForId,
  ScriptDiag_NoFuncFoundForId,
  ScriptDiag_IncorrectArgCountForBuiltinFunc,
  ScriptDiag_UnclosedParenthesizedExpr,
  ScriptDiag_UnterminatedBlock,
  ScriptDiag_UnterminatedArgumentList,
  ScriptDiag_BlockTooBig,
  ScriptDiag_MissingSemicolon,
  ScriptDiag_UnexpectedSemicolon,
  ScriptDiag_UnnecessarySemicolon,
  ScriptDiag_ArgumentCountExceedsMaximum,
  ScriptDiag_InvalidConditionCount,
  ScriptDiag_InvalidIf,
  ScriptDiag_InvalidWhileLoop,
  ScriptDiag_InvalidForLoop,
  ScriptDiag_ForLoopCompMissing,
  ScriptDiag_ForLoopCompStatic,
  ScriptDiag_ForLoopSeparatorMissing,
  ScriptDiag_BlockExpected,
  ScriptDiag_BlockOrIfExpected,
  ScriptDiag_MissingColonInSelectExpr,
  ScriptDiag_UnexpectedTokenAfterExpr,
  ScriptDiag_OnlyValidInLoop,
  ScriptDiag_VarUnused,
  ScriptDiag_ExprHasNoEffect,
  ScriptDiag_ExprUnreachable,
  ScriptDiag_ConditionExprStatic,
  ScriptDiag_TooFewArguments,
  ScriptDiag_TooManyArguments,
  ScriptDiag_InvalidArgumentValue,
  ScriptDiag_MemoryAccessDisallowed,

  ScriptDiagKind_Count,
} ScriptDiagKind;

typedef enum {
  ScriptDiagSeverity_Error,
  ScriptDiagSeverity_Warning,
} ScriptDiagSeverity;

typedef enum {
  ScriptDiagFilter_None    = 0,
  ScriptDiagFilter_Error   = 1 << 0,
  ScriptDiagFilter_Warning = 1 << 1,
  ScriptDiagFilter_All     = ~0,
} ScriptDiagFilter;

typedef struct {
  ScriptDiagSeverity severity : 8;
  ScriptDiagKind     kind : 8;
  ScriptRange        range;
} ScriptDiag;

typedef struct sScriptDiagBag ScriptDiagBag;

ScriptDiagBag* script_diag_bag_create(Allocator*, ScriptDiagFilter);
void           script_diag_bag_destroy(ScriptDiagBag*);

bool              script_diag_active(const ScriptDiagBag*, ScriptDiagSeverity);
const ScriptDiag* script_diag_data(const ScriptDiagBag*);
u32               script_diag_count(const ScriptDiagBag*, ScriptDiagFilter);
const ScriptDiag* script_diag_first(const ScriptDiagBag*, ScriptDiagFilter);

bool script_diag_push(ScriptDiagBag*, const ScriptDiag*);
void script_diag_clear(ScriptDiagBag*);

String script_diag_msg_scratch(const ScriptLookup*, const ScriptDiag*);
void   script_diag_pretty_write(DynString*, const ScriptLookup*, const ScriptDiag*);
String script_diag_pretty_scratch(const ScriptLookup*, const ScriptDiag*);
