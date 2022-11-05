#include "core_array.h"
#include "core_diag.h"
#include "script_operation.h"

String script_op_unary_str(const ScriptOpUnary c) {
  diag_assert(c < ScriptOpUnary_Count);
  static const String g_names[] = {
      string_static("negate"),
      string_static("invert"),
      string_static("normalize"),
      string_static("get-x"),
      string_static("get-y"),
      string_static("get-z"),
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
      string_static("logic-and"),
      string_static("logic-or"),
      string_static("null-coalescing"),
      string_static("add"),
      string_static("sub"),
      string_static("mul"),
      string_static("div"),
      string_static("distance"),
      string_static("ret-right"),
  };
  ASSERT(array_elems(g_names) == ScriptOpBinary_Count, "Incorrect number of names");
  return g_names[c];
}

String script_op_ternary_str(const ScriptOpTernary c) {
  diag_assert(c < ScriptOpTernary_Count);
  static const String g_names[] = {
      string_static("compose-vector3"),
  };
  ASSERT(array_elems(g_names) == ScriptOpTernary_Count, "Incorrect number of names");
  return g_names[c];
}
