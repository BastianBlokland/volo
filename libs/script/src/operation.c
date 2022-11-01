#include "core_array.h"
#include "core_diag.h"
#include "script_operation.h"

String script_op_unary_str(const ScriptOpUnary c) {
  diag_assert(c < ScriptOpUnary_Count);
  static const String g_names[] = {
      string_static("negate"),
  };
  ASSERT(array_elems(g_names) == ScriptOpUnary_Count, "Incorrect number of names");
  return g_names[c];
}

String script_op_binary_str(const ScriptOpBinary c) {
  diag_assert(c < ScriptOpBinary_Count);
  static const String g_names[] = {
      string_static("equal"),
      string_static("not-equal"),
      string_static("less"),
      string_static("less-or-equal"),
      string_static("greater"),
      string_static("greater-or-equal"),
      string_static("add"),
      string_static("sub"),
  };
  ASSERT(array_elems(g_names) == ScriptOpBinary_Count, "Incorrect number of names");
  return g_names[c];
}
