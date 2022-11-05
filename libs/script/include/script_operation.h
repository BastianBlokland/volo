#pragma once
#include "script_val.h"

typedef enum {
  ScriptOpUnary_Negate,
  ScriptOpUnary_Invert,

  ScriptOpUnary_Count,
} ScriptOpUnary;

typedef enum {
  ScriptOpBinary_Equal,
  ScriptOpBinary_NotEqual,
  ScriptOpBinary_Less,
  ScriptOpBinary_LessOrEqual,
  ScriptOpBinary_Greater,
  ScriptOpBinary_GreaterOrEqual,
  ScriptOpBinary_LogicAnd,
  ScriptOpBinary_LogicOr,
  ScriptOpBinary_NullCoalescing,
  ScriptOpBinary_Add,
  ScriptOpBinary_Sub,
  ScriptOpBinary_Mul,
  ScriptOpBinary_Div,
  ScriptOpBinary_Distance,
  ScriptOpBinary_RetRight,

  ScriptOpBinary_Count,
} ScriptOpBinary;

typedef enum {
  ScriptOpTernary_ComposeVector3,

  ScriptOpTernary_Count,
} ScriptOpTernary;

/**
 * Get a textual representation of the given operation.
 */
String script_op_unary_str(ScriptOpUnary);
String script_op_binary_str(ScriptOpBinary);
String script_op_ternary_str(ScriptOpTernary);

/**
 * Create a formatting argument for an operation type.
 */
#define script_op_unary_fmt(_VAL_) fmt_text(script_op_unary_str(_VAL_))
#define script_op_binary_fmt(_VAL_) fmt_text(script_op_binary_str(_VAL_))
#define script_op_ternary_fmt(_VAL_) fmt_text(script_op_ternary_str(_VAL_))
