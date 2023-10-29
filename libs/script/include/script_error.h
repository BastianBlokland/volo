#pragma once
#include "core_types.h"

// Forward declare from 'script_panic.h'.
typedef enum eScriptPanicType ScriptPanicType;

#define script_error_arg_sentinel sentinel_u16

typedef enum {
  ScriptError_None,
  ScriptError_InvalidArgument,
  ScriptError_MissingArgument,
  ScriptError_InvalidEnumEntry,
} ScriptErrorType;

typedef struct sScriptError {
  ScriptErrorType type : 16;
  u16             argIndex;
} ScriptError;

ScriptPanicType script_error_to_panic(ScriptErrorType);
