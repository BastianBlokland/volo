#pragma once
#include "script_val.h"

typedef enum {
  ScriptOpUnary_Negate,

  ScriptOpUnary_Count,
} ScriptOpUnary;

typedef enum {
  ScriptOpBinary_Equal,
  ScriptOpBinary_NotEqual,
  ScriptOpBinary_Less,
  ScriptOpBinary_LessOrEqual,
  ScriptOpBinary_Greater,
  ScriptOpBinary_GreaterOrEqual,
  ScriptOpBinary_Add,
  ScriptOpBinary_Sub,

  ScriptOpBinary_Count,
} ScriptOpBinary;

/**
 * Perform an operation.
 */
ScriptVal script_op_unary(ScriptVal, ScriptOpUnary);
ScriptVal script_op_binary(ScriptVal, ScriptVal, ScriptOpBinary);

/**
 * Get a textual representation of the given operation.
 */
String script_op_unary_str(ScriptOpUnary);
String script_op_binary_str(ScriptOpBinary);

/**
 * Create a formatting argument for an operation type.
 */
#define script_op_unary_fmt(_VAL_) fmt_text(script_op_unary_str(_VAL_))
#define script_op_binary_fmt(_VAL_) fmt_text(script_op_binary_str(_VAL_))
