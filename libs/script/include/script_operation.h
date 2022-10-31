#pragma once
#include "script_val.h"

typedef enum {
  ScriptOpBin_Equal,
  ScriptOpBin_NotEqual,
  ScriptOpBin_Less,
  ScriptOpBin_LessOrEqual,
  ScriptOpBin_Greater,
  ScriptOpBin_GreaterOrEqual,

  ScriptOpBin_Count,
} ScriptOpBin;

/**
 * Perform an operation.
 */
ScriptVal script_op_bin(ScriptVal, ScriptVal, ScriptOpBin);

/**
 * Get a textual representation of the given operation.
 */
String script_op_bin_str(ScriptOpBin);

/**
 * Create a formatting argument for an operation type.
 */
#define script_op_bin_fmt(_VAL_) fmt_text(script_op_bin_str(_VAL_))
