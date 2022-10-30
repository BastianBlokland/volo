#pragma once
#include "core_string.h"

typedef enum {
  ScriptResult_Success,
  ScriptResult_Fail,
} ScriptResult;

/**
 * Return a textual representation of the given ScriptResult.
 */
String script_result_str(ScriptResult);
