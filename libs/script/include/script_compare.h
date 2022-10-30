#pragma once
#include "script_val.h"

typedef enum {
  ScriptComparison_Equal,
  ScriptComparison_NotEqual,
  ScriptComparison_Less,
  ScriptComparison_LessOrEqual,
  ScriptComparison_Greater,
  ScriptComparison_GreaterOrEqual,

  ScriptComparison_Count,
} ScriptComparison;

/**
 * Compare two script values.
 */
bool script_compare(ScriptVal, ScriptVal, ScriptComparison);

/**
 * Get a textual representation of the given comparision type.
 */
String script_comparision_str(ScriptComparison);

/**
 * Create a formatting argument for a comparision type.
 */
#define script_comparision_fmt(_VAL_) fmt_text(script_comparision_str(_VAL_))
