#pragma once
#include "core_types.h"

// Forward declare from 'script_panic.h'.
typedef enum eScriptPanicType ScriptPanicType;

#define script_error_arg_sentinel sentinel_u16

typedef enum {
  ScriptError_None,
  ScriptError_ArgumentInvalid,
  ScriptError_ArgumentMissing,
  ScriptError_ArgumentOutOfRange,
  ScriptError_EnumInvalidEntry,
} ScriptErrorType;

typedef struct sScriptError {
  ScriptErrorType type : 16;
  u16             argIndex;
} ScriptError;

ScriptError script_error(ScriptErrorType);
ScriptError script_error_arg(ScriptErrorType, u16 argIndex);

ScriptPanicType script_error_to_panic(ScriptErrorType);
