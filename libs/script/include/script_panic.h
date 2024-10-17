#pragma once
#include "script_pos.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef enum eScriptPanicKind {
  ScriptPanic_None,
  ScriptPanic_AssertionFailed,
  ScriptPanic_ExecutionFailed,
  ScriptPanic_ExecutionLimitExceeded,
  ScriptPanic_ArgumentInvalid,
  ScriptPanic_ArgumentNull,
  ScriptPanic_ArgumentMissing,
  ScriptPanic_ArgumentOutOfRange,
  ScriptPanic_ArgumentCountExceedsMaximum,
  ScriptPanic_EnumInvalidEntry,
  ScriptPanic_UnimplementedBinding,
  ScriptPanic_QueryLimitExceeded,
  ScriptPanic_QueryInvalid,
  ScriptPanic_ReadonlyParam,
  ScriptPanic_MissingCapability,

  ScriptPanicKind_Count,
} ScriptPanicKind;

typedef struct sScriptPanic {
  ScriptPanicKind kind;
  ScriptRange     range;
} ScriptPanic;

bool   script_panic_valid(const ScriptPanic*);
String script_panic_kind_str(ScriptPanicKind);
void   script_panic_pretty_write(DynString*, String sourceText, const ScriptPanic*);
String script_panic_pretty_scratch(String sourceText, const ScriptPanic*);
