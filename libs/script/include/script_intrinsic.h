#pragma once
#include "core_string.h"

typedef enum {
  ScriptIntrinsic_Continue,          // Args: none.
  ScriptIntrinsic_Break,             // Args: none.
  ScriptIntrinsic_Return,            // Args: value.
  ScriptIntrinsic_Type,              // Args: value.
  ScriptIntrinsic_Assert,            // Args: condition.
  ScriptIntrinsic_Select,            // Args: condition, if branch, else branch.
  ScriptIntrinsic_NullCoalescing,    // Args: lhs, rhs.
  ScriptIntrinsic_LogicAnd,          // Args: lhs, rhs.
  ScriptIntrinsic_LogicOr,           // Args: lhs, rhs.
  ScriptIntrinsic_Loop,              // Args: setup, condition, increment, body.
  ScriptIntrinsic_Equal,             // Args: lhs, rhs.
  ScriptIntrinsic_NotEqual,          // Args: lhs, rhs.
  ScriptIntrinsic_Less,              // Args: lhs, rhs.
  ScriptIntrinsic_LessOrEqual,       // Args: lhs, rhs.
  ScriptIntrinsic_Greater,           // Args: lhs, rhs.
  ScriptIntrinsic_GreaterOrEqual,    // Args: lhs, rhs.
  ScriptIntrinsic_Add,               // Args: lhs, rhs.
  ScriptIntrinsic_Sub,               // Args: lhs, rhs.
  ScriptIntrinsic_Mul,               // Args: lhs, rhs.
  ScriptIntrinsic_Div,               // Args: lhs, rhs.
  ScriptIntrinsic_Mod,               // Args: lhs, rhs.
  ScriptIntrinsic_Negate,            // Args: value.
  ScriptIntrinsic_Invert,            // Args: value.
  ScriptIntrinsic_Distance,          // Args: lhs, rhs.
  ScriptIntrinsic_Angle,             // Args: lhs, rhs.
  ScriptIntrinsic_Normalize,         // Args: value.
  ScriptIntrinsic_Magnitude,         // Args: value.
  ScriptIntrinsic_VectorX,           // Args: value.
  ScriptIntrinsic_VectorY,           // Args: value.
  ScriptIntrinsic_VectorZ,           // Args: value.
  ScriptIntrinsic_Vector3Compose,    // Args: x, y, z.
  ScriptIntrinsic_QuatFromEuler,     // Args: x, y, z.
  ScriptIntrinsic_QuatFromAngleAxis, // Args: angle, axis.
  ScriptIntrinsic_Random,            // Args: none.
  ScriptIntrinsic_RandomSphere,      // Args: none.
  ScriptIntrinsic_RandomCircleXZ,    // Args: none.
  ScriptIntrinsic_RandomBetween,     // Args: min, max.
  ScriptIntrinsic_RoundDown,         // Args: value.
  ScriptIntrinsic_RoundNearest,      // Args: value.
  ScriptIntrinsic_RoundUp,           // Args: value.

  ScriptIntrinsic_Count,
} ScriptIntrinsic;

/**
 * Return how many arguments an intrinsic takes.
 */
u32 script_intrinsic_arg_count(ScriptIntrinsic);
u32 script_intrinsic_arg_count_always_reached(ScriptIntrinsic);

/**
 * Get a textual representation of the given intrinsic.
 */
String script_intrinsic_str(ScriptIntrinsic);

/**
 * Create a formatting argument for an intrinsic.
 */
#define script_intrinsic_fmt(_VAL_) fmt_text(script_intrinsic_str(_VAL_))
