#include "core_array.h"
#include "core_diag.h"
#include "script_operation.h"

String script_op_nullary_str(const ScriptOpNullary o) {
  diag_assert(o < ScriptOpNullary_Count);
  static const String g_names[] = {
      string_static("random"),
  };
  ASSERT(array_elems(g_names) == ScriptOpNullary_Count, "Incorrect number of names");
  return g_names[o];
}

String script_op_unary_str(const ScriptOpUnary o) {
  diag_assert(o < ScriptOpUnary_Count);
  static const String g_names[] = {
      string_static("negate"),
      string_static("invert"),
      string_static("normalize"),
      string_static("magnitude"),
      string_static("vector-x"),
      string_static("vector-y"),
      string_static("vector-z"),
  };
  ASSERT(array_elems(g_names) == ScriptOpUnary_Count, "Incorrect number of names");
  return g_names[o];
}

String script_op_binary_str(const ScriptOpBinary o) {
  diag_assert(o < ScriptOpBinary_Count);
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
      string_static("angle"),
      string_static("ret-right"),
      string_static("random-between"),
  };
  ASSERT(array_elems(g_names) == ScriptOpBinary_Count, "Incorrect number of names");
  return g_names[o];
}

String script_op_ternary_str(const ScriptOpTernary o) {
  diag_assert(o < ScriptOpTernary_Count);
  static const String g_names[] = {
      string_static("compose-vector3"),
      string_static("select"),
  };
  ASSERT(array_elems(g_names) == ScriptOpTernary_Count, "Incorrect number of names");
  return g_names[o];
}
