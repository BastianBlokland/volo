#pragma once
#include "core_string.h"

typedef enum eScriptErrorRuntime {
  ScriptErrorRuntime_None,
  ScriptErrorRuntime_AssertionFailed,
  ScriptErrorRuntime_ExecutionLimitExceeded,

  ScriptErrorRuntime_Count,
} ScriptErrorRuntime;

/**
 * Return a textual representation of the given ScriptError.
 */
String script_error_runtime_str(ScriptErrorRuntime);

/**
 * Create a formatting argument for a script error.
 */
#define script_error_runtime_fmt(_VAL_) fmt_text(script_error_runtime_str(_VAL_))
