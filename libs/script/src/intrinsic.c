#include "core_array.h"
#include "core_diag.h"
#include "script_intrinsic.h"

u32 script_intrinsic_arg_count(const ScriptIntrinsic i) {
  switch (i) {
  case ScriptIntrinsic_Random:
    return 0;
  case ScriptIntrinsic_Negate:
  case ScriptIntrinsic_Invert:
  case ScriptIntrinsic_Normalize:
  case ScriptIntrinsic_Magnitude:
  case ScriptIntrinsic_VectorX:
  case ScriptIntrinsic_VectorY:
  case ScriptIntrinsic_VectorZ:
  case ScriptIntrinsic_RoundDown:
  case ScriptIntrinsic_RoundNearest:
  case ScriptIntrinsic_RoundUp:
  case ScriptIntrinsic_Assert:
    return 1;
  case ScriptIntrinsic_Equal:
  case ScriptIntrinsic_NotEqual:
  case ScriptIntrinsic_Less:
  case ScriptIntrinsic_LessOrEqual:
  case ScriptIntrinsic_Greater:
  case ScriptIntrinsic_GreaterOrEqual:
  case ScriptIntrinsic_NullCoalescing:
  case ScriptIntrinsic_Add:
  case ScriptIntrinsic_Sub:
  case ScriptIntrinsic_Mul:
  case ScriptIntrinsic_Div:
  case ScriptIntrinsic_Mod:
  case ScriptIntrinsic_Distance:
  case ScriptIntrinsic_Angle:
  case ScriptIntrinsic_RandomBetween:
  case ScriptIntrinsic_While:
  case ScriptIntrinsic_LogicAnd:
  case ScriptIntrinsic_LogicOr:
    return 2;
  case ScriptIntrinsic_ComposeVector3:
  case ScriptIntrinsic_If:
  case ScriptIntrinsic_Select:
    return 3;
  case ScriptIntrinsic_Count:
    break;
  }
  diag_assert_fail("Unknown intrinsic type");
  UNREACHABLE
}

String script_intrinsic_str(const ScriptIntrinsic i) {
  diag_assert(i < ScriptIntrinsic_Count);
  static const String g_names[] = {
      string_static("random"),
      string_static("negate"),
      string_static("invert"),
      string_static("normalize"),
      string_static("magnitude"),
      string_static("vector-x"),
      string_static("vector-y"),
      string_static("vector-z"),
      string_static("round-down"),
      string_static("round-nearest"),
      string_static("round-up"),
      string_static("assert"),
      string_static("equal"),
      string_static("not-equal"),
      string_static("less"),
      string_static("less-or-equal"),
      string_static("greater"),
      string_static("greater-or-equal"),
      string_static("null-coalescing"),
      string_static("add"),
      string_static("sub"),
      string_static("mul"),
      string_static("div"),
      string_static("mod"),
      string_static("distance"),
      string_static("angle"),
      string_static("random-between"),
      string_static("while"),
      string_static("logic-and"),
      string_static("logic-or"),
      string_static("compose-vector3"),
      string_static("if"),
      string_static("select"),
  };
  ASSERT(array_elems(g_names) == ScriptIntrinsic_Count, "Incorrect number of names");
  return g_names[i];
}
