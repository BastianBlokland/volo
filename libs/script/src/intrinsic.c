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
  case ScriptIntrinsic_Invert:
  case ScriptIntrinsic_Magnitude:
  case ScriptIntrinsic_Negate:
  case ScriptIntrinsic_Normalize:
  case ScriptIntrinsic_RoundDown:
  case ScriptIntrinsic_RoundNearest:
  case ScriptIntrinsic_RoundUp:
  case ScriptIntrinsic_Type:
  case ScriptIntrinsic_VectorX:
  case ScriptIntrinsic_VectorY:
  case ScriptIntrinsic_VectorZ:
    return 1;
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
    return 2;
  case ScriptIntrinsic_QuatFromEuler:
  case ScriptIntrinsic_Select:
  case ScriptIntrinsic_Vector3Compose:
    return 3;
  case ScriptIntrinsic_Loop:
    return 4;
  case ScriptIntrinsic_Count:
    break;
  }
  diag_assert_fail("Unknown intrinsic type");
  UNREACHABLE
}

String script_intrinsic_str(const ScriptIntrinsic i) {
  diag_assert(i < ScriptIntrinsic_Count);
  static const String g_names[] = {
      [ScriptIntrinsic_Continue]          = string_static("continue"),
      [ScriptIntrinsic_Break]             = string_static("break"),
      [ScriptIntrinsic_Return]            = string_static("return"),
      [ScriptIntrinsic_Type]              = string_static("type"),
      [ScriptIntrinsic_Assert]            = string_static("assert"),
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
      [ScriptIntrinsic_Normalize]         = string_static("normalize"),
      [ScriptIntrinsic_Magnitude]         = string_static("magnitude"),
      [ScriptIntrinsic_VectorX]           = string_static("vector-x"),
      [ScriptIntrinsic_VectorY]           = string_static("vector-y"),
      [ScriptIntrinsic_VectorZ]           = string_static("vector-z"),
      [ScriptIntrinsic_Vector3Compose]    = string_static("vector3-compose"),
      [ScriptIntrinsic_QuatFromEuler]     = string_static("quat-from-euler"),
      [ScriptIntrinsic_QuatFromAngleAxis] = string_static("quat-from-angle-axis"),
      [ScriptIntrinsic_Random]            = string_static("random"),
      [ScriptIntrinsic_RandomSphere]      = string_static("random-sphere"),
      [ScriptIntrinsic_RandomCircleXZ]    = string_static("random-circle-xz"),
      [ScriptIntrinsic_RandomBetween]     = string_static("random-between"),
      [ScriptIntrinsic_RoundDown]         = string_static("round-down"),
      [ScriptIntrinsic_RoundNearest]      = string_static("round-nearest"),
      [ScriptIntrinsic_RoundUp]           = string_static("round-up"),
  };
  ASSERT(array_elems(g_names) == ScriptIntrinsic_Count, "Incorrect number of names");
  return g_names[i];
}
