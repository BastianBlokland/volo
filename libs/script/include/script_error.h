#pragma once
#include "core_string.h"

typedef enum {
  ScriptError_InvalidChar,

  ScriptError_Count,
} ScriptError;

/**
 * Return a textual representation of the given ScriptError.
 */
String script_error_str(ScriptError);
