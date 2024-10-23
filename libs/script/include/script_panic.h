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
  ScriptPanicKind    kind : 16;
  u16                argIndex;
  ScriptRangeLineCol range;
} ScriptPanic;

typedef enum {
  ScriptPanicOutput_IncludeRange = 1 << 0,
} ScriptPanicOutputFlags;

String script_panic_kind_str(ScriptPanicKind);
void   script_panic_write(DynString*, const ScriptPanic*, ScriptPanicOutputFlags);
String script_panic_scratch(const ScriptPanic*, ScriptPanicOutputFlags);
