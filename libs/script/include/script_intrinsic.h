#pragma once
#include "core.h"

typedef enum eScriptIntrinsic {
  ScriptIntrinsic_Continue,          // Args: none.
  ScriptIntrinsic_Break,             // Args: none.
  ScriptIntrinsic_Return,            // Args: value.
  ScriptIntrinsic_Type,              // Args: value.
  ScriptIntrinsic_Hash,              // Args: value.
  ScriptIntrinsic_Assert,            // Args: condition.
  ScriptIntrinsic_MemLoadDynamic,    // Args: key.
  ScriptIntrinsic_MemStoreDynamic,   // Args: key, value.
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
  ScriptIntrinsic_Sin,               // Args: value.
  ScriptIntrinsic_Cos,               // Args: value.
  ScriptIntrinsic_Normalize,         // Args: value.
  ScriptIntrinsic_Magnitude,         // Args: value.
  ScriptIntrinsic_Absolute,          // Args: value.
  ScriptIntrinsic_VecX,              // Args: value.
  ScriptIntrinsic_VecY,              // Args: value.
  ScriptIntrinsic_VecZ,              // Args: value.
  ScriptIntrinsic_Vec3Compose,       // Args: x, y, z.
  ScriptIntrinsic_QuatFromEuler,     // Args: x, y, z.
  ScriptIntrinsic_QuatFromAngleAxis, // Args: angle, axis.
  ScriptIntrinsic_ColorCompose,      // Args: r, g, b, a.
  ScriptIntrinsic_ColorComposeHsv,   // Args: h, s, v, a.
  ScriptIntrinsic_ColorFor,          // Args: value.
  ScriptIntrinsic_Random,            // Args: none.
  ScriptIntrinsic_RandomSphere,      // Args: none.
  ScriptIntrinsic_RandomCircleXZ,    // Args: none.
  ScriptIntrinsic_RandomBetween,     // Args: min, max.
  ScriptIntrinsic_RoundDown,         // Args: value.
  ScriptIntrinsic_RoundNearest,      // Args: value.
  ScriptIntrinsic_RoundUp,           // Args: value.
  ScriptIntrinsic_Clamp,             // Args: value, min, max.
  ScriptIntrinsic_Lerp,              // Args: x, y, t.
  ScriptIntrinsic_Min,               // Args: x, y.
  ScriptIntrinsic_Max,               // Args: x, y.
  ScriptIntrinsic_Perlin3,           // Args: position

  ScriptIntrinsic_Count,
} ScriptIntrinsic;

/**
 * Intrinsic traits.
 */
u32  script_intrinsic_arg_count(ScriptIntrinsic);
u32  script_intrinsic_arg_count_always_reached(ScriptIntrinsic);
bool script_intrinsic_deterministic(ScriptIntrinsic);

/**
 * Get a textual representation of the given intrinsic.
 */
String script_intrinsic_str(ScriptIntrinsic);

/**
 * Create a formatting argument for an intrinsic.
 */
#define script_intrinsic_fmt(_VAL_) fmt_text(script_intrinsic_str(_VAL_))
