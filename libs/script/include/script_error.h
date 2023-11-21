#pragma once
#include "core_types.h"

// Forward declare from 'script_panic.h'.
typedef enum eScriptPanicKind ScriptPanicKind;

#define script_error_arg_sentinel sentinel_u16

typedef enum {
  ScriptError_None,
  ScriptError_ArgumentInvalid,
  ScriptError_ArgumentMissing,
  ScriptError_ArgumentOutOfRange,
  ScriptError_ArgumentCountExceedsMaximum,
  ScriptError_EnumInvalidEntry,
  ScriptError_UnimplementedBinding,

  ScriptErrorKind_Count,
} ScriptErrorKind;

typedef struct sScriptError {
  ScriptErrorKind kind : 16;
  u16             argIndex;
} ScriptError;

ScriptError script_error(ScriptErrorKind);
ScriptError script_error_arg(ScriptErrorKind, u16 argIndex);
bool        script_error_valid(const ScriptError*);

ScriptPanicKind script_error_to_panic(ScriptErrorKind);
