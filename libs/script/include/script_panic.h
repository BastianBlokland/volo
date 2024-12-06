#pragma once
#include "script_pos.h"
#include "script_val.h"

typedef enum eScriptPanicKind {
  ScriptPanic_None,
  ScriptPanic_AssertionFailed,
  ScriptPanic_ExecutionFailed,
  ScriptPanic_ExecutionLimitExceeded,
  ScriptPanic_ArgumentInvalid,
  ScriptPanic_ArgumentTypeMismatch,
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
  ScriptMask         typeMask;
  ScriptType         typeActual : 16;
  u32                contextInt;
  ScriptRangeLineCol range;
} ScriptPanic;

typedef enum {
  ScriptPanicOutput_Default      = 0,
  ScriptPanicOutput_IncludeRange = 1 << 0,
} ScriptPanicOutputFlags;

typedef struct sScriptPanicHandler ScriptPanicHandler;

NORETURN void script_panic_raise(ScriptPanicHandler*, ScriptPanic);

void   script_panic_write(DynString*, const ScriptPanic*, ScriptPanicOutputFlags);
String script_panic_scratch(const ScriptPanic*, ScriptPanicOutputFlags);
