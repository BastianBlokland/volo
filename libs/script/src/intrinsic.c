#include "core_array.h"
#include "core_diag.h"
#include "script_intrinsic.h"

u32 script_intrinsic_arg_count(const ScriptIntrinsic i) {
  switch (i) {
  case ScriptIntrinsic_Break:
  case ScriptIntrinsic_Continue:
  case ScriptIntrinsic_Random:
  case ScriptIntrinsic_RandomCircleXZ:
  case ScriptIntrinsic_RandomSphere:
    return 0;
  case ScriptIntrinsic_Return:
  case ScriptIntrinsic_Assert:
  case ScriptIntrinsic_MemLoadDynamic:
  case ScriptIntrinsic_Invert:
  case ScriptIntrinsic_Magnitude:
  case ScriptIntrinsic_Absolute:
  case ScriptIntrinsic_Negate:
  case ScriptIntrinsic_Sin:
  case ScriptIntrinsic_Cos:
  case ScriptIntrinsic_Normalize:
  case ScriptIntrinsic_RoundDown:
  case ScriptIntrinsic_RoundNearest:
  case ScriptIntrinsic_RoundUp:
  case ScriptIntrinsic_Type:
  case ScriptIntrinsic_Hash:
  case ScriptIntrinsic_VecX:
  case ScriptIntrinsic_VecY:
  case ScriptIntrinsic_VecZ:
  case ScriptIntrinsic_ColorR:
  case ScriptIntrinsic_ColorG:
  case ScriptIntrinsic_ColorB:
  case ScriptIntrinsic_ColorA:
  case ScriptIntrinsic_ColorFor:
  case ScriptIntrinsic_Perlin3:
    return 1;
  case ScriptIntrinsic_MemStoreDynamic:
  case ScriptIntrinsic_Add:
  case ScriptIntrinsic_Angle:
  case ScriptIntrinsic_Distance:
  case ScriptIntrinsic_Div:
  case ScriptIntrinsic_Equal:
  case ScriptIntrinsic_Greater:
  case ScriptIntrinsic_GreaterOrEqual:
  case ScriptIntrinsic_Less:
  case ScriptIntrinsic_LessOrEqual:
  case ScriptIntrinsic_LogicAnd:
  case ScriptIntrinsic_LogicOr:
  case ScriptIntrinsic_Mod:
  case ScriptIntrinsic_Mul:
  case ScriptIntrinsic_NotEqual:
  case ScriptIntrinsic_NullCoalescing:
  case ScriptIntrinsic_QuatFromAngleAxis:
  case ScriptIntrinsic_RandomBetween:
  case ScriptIntrinsic_Sub:
  case ScriptIntrinsic_Min:
  case ScriptIntrinsic_Max:
    return 2;
  case ScriptIntrinsic_QuatFromEuler:
  case ScriptIntrinsic_Select:
  case ScriptIntrinsic_Vec3Compose:
  case ScriptIntrinsic_Clamp:
  case ScriptIntrinsic_Lerp:
    return 3;
  case ScriptIntrinsic_Loop:
  case ScriptIntrinsic_ColorCompose:
  case ScriptIntrinsic_ColorComposeHsv:
    return 4;
  case ScriptIntrinsic_Count:
    break;
  }
  diag_assert_fail("Unknown intrinsic type");
  UNREACHABLE
}

u32 script_intrinsic_arg_count_always_reached(const ScriptIntrinsic i) {
  switch (i) {
  case ScriptIntrinsic_Select:         // Always reached args: condition.
  case ScriptIntrinsic_NullCoalescing: // Always reached args: lhs.
  case ScriptIntrinsic_LogicAnd:       // Always reached args: lhs.
  case ScriptIntrinsic_LogicOr:        // Always reached args: lhs.
    return 1;                          //
  case ScriptIntrinsic_Loop:           // Always reached args: setup, condition.
    return 2;                          //
  default:                             // Always reached args: all.
    return script_intrinsic_arg_count(i);
  }
}

bool script_intrinsic_deterministic(const ScriptIntrinsic i) {
  switch (i) {
  case ScriptIntrinsic_Continue:
  case ScriptIntrinsic_Break:
  case ScriptIntrinsic_Return:
  case ScriptIntrinsic_Assert:
  case ScriptIntrinsic_MemLoadDynamic:
  case ScriptIntrinsic_MemStoreDynamic:
  case ScriptIntrinsic_Random:
  case ScriptIntrinsic_RandomSphere:
  case ScriptIntrinsic_RandomCircleXZ:
  case ScriptIntrinsic_RandomBetween:
    return false;
  default:
    return true;
  }
}

String script_intrinsic_str(const ScriptIntrinsic i) {
  diag_assert(i < ScriptIntrinsic_Count);
  static const String g_names[] = {
      [ScriptIntrinsic_Continue]          = string_static("continue"),
      [ScriptIntrinsic_Break]             = string_static("break"),
      [ScriptIntrinsic_Return]            = string_static("return"),
      [ScriptIntrinsic_Type]              = string_static("type"),
      [ScriptIntrinsic_Hash]              = string_static("hash"),
      [ScriptIntrinsic_Assert]            = string_static("assert"),
      [ScriptIntrinsic_MemLoadDynamic]    = string_static("mem-load-dynamic"),
      [ScriptIntrinsic_MemStoreDynamic]   = string_static("mem-store-dynamic"),
      [ScriptIntrinsic_Select]            = string_static("select"),
      [ScriptIntrinsic_NullCoalescing]    = string_static("null-coalescing"),
      [ScriptIntrinsic_LogicAnd]          = string_static("logic-and"),
      [ScriptIntrinsic_LogicOr]           = string_static("logic-or"),
      [ScriptIntrinsic_Loop]              = string_static("loop"),
      [ScriptIntrinsic_Equal]             = string_static("equal"),
      [ScriptIntrinsic_NotEqual]          = string_static("not-equal"),
      [ScriptIntrinsic_Less]              = string_static("less"),
      [ScriptIntrinsic_LessOrEqual]       = string_static("less-or-equal"),
      [ScriptIntrinsic_Greater]           = string_static("greater"),
      [ScriptIntrinsic_GreaterOrEqual]    = string_static("greater-or-equal"),
      [ScriptIntrinsic_Add]               = string_static("add"),
      [ScriptIntrinsic_Sub]               = string_static("sub"),
      [ScriptIntrinsic_Mul]               = string_static("mul"),
      [ScriptIntrinsic_Div]               = string_static("div"),
      [ScriptIntrinsic_Mod]               = string_static("mod"),
      [ScriptIntrinsic_Negate]            = string_static("negate"),
      [ScriptIntrinsic_Invert]            = string_static("invert"),
      [ScriptIntrinsic_Distance]          = string_static("distance"),
      [ScriptIntrinsic_Angle]             = string_static("angle"),
      [ScriptIntrinsic_Sin]               = string_static("sin"),
      [ScriptIntrinsic_Cos]               = string_static("cos"),
      [ScriptIntrinsic_Normalize]         = string_static("normalize"),
      [ScriptIntrinsic_Magnitude]         = string_static("magnitude"),
      [ScriptIntrinsic_Absolute]          = string_static("absolute"),
      [ScriptIntrinsic_VecX]              = string_static("vec-x"),
      [ScriptIntrinsic_VecY]              = string_static("vec-y"),
      [ScriptIntrinsic_VecZ]              = string_static("vec-z"),
      [ScriptIntrinsic_Vec3Compose]       = string_static("vec3-compose"),
      [ScriptIntrinsic_QuatFromEuler]     = string_static("quat-from-euler"),
      [ScriptIntrinsic_QuatFromAngleAxis] = string_static("quat-from-angle-axis"),
      [ScriptIntrinsic_ColorR]            = string_static("color-r"),
      [ScriptIntrinsic_ColorG]            = string_static("color-g"),
      [ScriptIntrinsic_ColorB]            = string_static("color-b"),
      [ScriptIntrinsic_ColorA]            = string_static("color-a"),
      [ScriptIntrinsic_ColorCompose]      = string_static("color-compose"),
      [ScriptIntrinsic_ColorComposeHsv]   = string_static("color-compose-hsv"),
      [ScriptIntrinsic_ColorFor]          = string_static("color-for"),
      [ScriptIntrinsic_Random]            = string_static("random"),
      [ScriptIntrinsic_RandomSphere]      = string_static("random-sphere"),
      [ScriptIntrinsic_RandomCircleXZ]    = string_static("random-circle-xz"),
      [ScriptIntrinsic_RandomBetween]     = string_static("random-between"),
      [ScriptIntrinsic_RoundDown]         = string_static("round-down"),
      [ScriptIntrinsic_RoundNearest]      = string_static("round-nearest"),
      [ScriptIntrinsic_RoundUp]           = string_static("round-up"),
      [ScriptIntrinsic_Clamp]             = string_static("clamp"),
      [ScriptIntrinsic_Lerp]              = string_static("lerp"),
      [ScriptIntrinsic_Min]               = string_static("min"),
      [ScriptIntrinsic_Max]               = string_static("max"),
      [ScriptIntrinsic_Perlin3]           = string_static("perlin3"),
  };
  ASSERT(array_elems(g_names) == ScriptIntrinsic_Count, "Incorrect number of names");
  return g_names[i];
}
