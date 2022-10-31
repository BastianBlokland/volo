#include "core_array.h"
#include "core_diag.h"
#include "script_operation.h"

ScriptVal script_op_bin(const ScriptVal a, const ScriptVal b, const ScriptOpBin c) {
  switch (c) {
  case ScriptOpBin_Equal:
    return script_bool(script_val_equal(a, b));
  case ScriptOpBin_NotEqual:
    return script_bool(!script_val_equal(a, b));
  case ScriptOpBin_Less:
    return script_bool(script_val_less(a, b));
  case ScriptOpBin_LessOrEqual:
    return script_bool(!script_val_greater(a, b));
  case ScriptOpBin_Greater:
    return script_bool(script_val_greater(a, b));
  case ScriptOpBin_GreaterOrEqual:
    return script_bool(!script_val_less(a, b));
  case ScriptOpBin_Count:
    break;
  }
  diag_assert_fail("Invalid comparison");
  UNREACHABLE
}

String script_op_bin_str(const ScriptOpBin c) {
  diag_assert(c < ScriptOpBin_Count);
  static const String g_names[] = {
      string_static("equal"),
      string_static("not-equal"),
      string_static("less"),
      string_static("less-or-equal"),
      string_static("greater"),
      string_static("greater-or-equal"),
  };
  ASSERT(array_elems(g_names) == ScriptOpBin_Count, "Incorrect number of names");
  return g_names[c];
}
