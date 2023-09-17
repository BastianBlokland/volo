#pragma once
#include "core_string.h"

typedef enum {
  ScriptIntrinsic_Random,
  ScriptIntrinsic_Negate,
  ScriptIntrinsic_Invert,
  ScriptIntrinsic_Normalize,
  ScriptIntrinsic_Magnitude,
  ScriptIntrinsic_VectorX,
  ScriptIntrinsic_VectorY,
  ScriptIntrinsic_VectorZ,
  ScriptIntrinsic_RoundDown,
  ScriptIntrinsic_RoundNearest,
  ScriptIntrinsic_RoundUp,
  ScriptIntrinsic_Equal,
  ScriptIntrinsic_NotEqual,
  ScriptIntrinsic_Less,
  ScriptIntrinsic_LessOrEqual,
  ScriptIntrinsic_Greater,
  ScriptIntrinsic_GreaterOrEqual,
  ScriptIntrinsic_NullCoalescing,
  ScriptIntrinsic_Add,
  ScriptIntrinsic_Sub,
  ScriptIntrinsic_Mul,
  ScriptIntrinsic_Div,
  ScriptIntrinsic_Mod,
  ScriptIntrinsic_Distance,
  ScriptIntrinsic_Angle,
  ScriptIntrinsic_RandomBetween,
  ScriptIntrinsic_While,
  ScriptIntrinsic_LogicAnd,
  ScriptIntrinsic_LogicOr,
  ScriptIntrinsic_ComposeVector3,
  ScriptIntrinsic_If,
  ScriptIntrinsic_Select,

  ScriptIntrinsic_Count,
} ScriptIntrinsic;

/**
 * Return how many arguments an intrinsic takes.
 */
u32 script_intrinsic_arg_count(ScriptIntrinsic);

/**
 * Get a textual representation of the given intrinsic.
 */
String script_intrinsic_str(ScriptIntrinsic);

/**
 * Create a formatting argument for an intrinsic.
 */
#define script_intrinsic_fmt(_VAL_) fmt_text(script_intrinsic_str(_VAL_))
