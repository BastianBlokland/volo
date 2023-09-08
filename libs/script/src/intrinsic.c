#include "core_array.h"
#include "core_diag.h"
#include "script_intrinsic.h"

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
      string_static("equal"),
      string_static("not-equal"),
      string_static("less"),
      string_static("less-or-equal"),
      string_static("greater"),
      string_static("greater-or-equal"),
      string_static("logic-and"),
      string_static("logic-or"),
      string_static("null-coalescing"),
      string_static("add"),
      string_static("sub"),
      string_static("mul"),
      string_static("div"),
      string_static("mod"),
      string_static("distance"),
      string_static("angle"),
      string_static("random-between"),
      string_static("compose-vector3"),
      string_static("select"),
  };
  ASSERT(array_elems(g_names) == ScriptIntrinsic_Count, "Incorrect number of names");
  return g_names[i];
}
