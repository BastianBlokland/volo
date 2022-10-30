#include "core_array.h"
#include "core_diag.h"
#include "script_compare.h"

bool script_compare(const ScriptVal a, const ScriptVal b, const ScriptComparison c) {
  switch (c) {
  case ScriptComparison_Equal:
    return script_val_equal(a, b);
  case ScriptComparison_NotEqual:
    return !script_val_equal(a, b);
  case ScriptComparison_Less:
    return script_val_less(a, b);
  case ScriptComparison_LessOrEqual:
    return !script_val_greater(a, b);
  case ScriptComparison_Greater:
    return script_val_greater(a, b);
  case ScriptComparison_GreaterOrEqual:
    return !script_val_less(a, b);
  case ScriptComparison_Count:
    break;
  }
  diag_assert_fail("Invalid comparison");
  UNREACHABLE
}

String script_comparision_str(const ScriptComparison c) {
  diag_assert(c < ScriptComparison_Count);
  static const String g_names[] = {
      string_static("equal"),
      string_static("not-equal"),
      string_static("less"),
      string_static("less-or-equal"),
      string_static("greater"),
      string_static("greater-or-equal"),
  };
  ASSERT(array_elems(g_names) == ScriptComparison_Count, "Incorrect number of names");
  return g_names[c];
}
