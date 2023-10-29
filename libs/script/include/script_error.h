#pragma once
#include "core_types.h"

// Forward declare from 'script_panic.h'.
typedef enum eScriptPanicType ScriptPanicType;

typedef enum {
  ScriptError_None,
  ScriptError_InvalidValue,
} ScriptErrorType;

typedef struct sScriptError {
  ScriptErrorType type : 8;
  u8              argIndex;
} ScriptError;

ScriptPanicType script_error_to_panic(ScriptErrorType);
