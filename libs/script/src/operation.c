#include "core_array.h"
#include "core_diag.h"
#include "script_operation.h"

ScriptVal script_op_unary(const ScriptVal val, const ScriptOpUnary op) {
  switch (op) {
  case ScriptOpUnary_Negate:
    return script_val_neg(val);
  case ScriptOpUnary_Count:
    break;
  }
  diag_assert_fail("Invalid unary operation");
  UNREACHABLE
}

ScriptVal script_op_binary(const ScriptVal a, const ScriptVal b, const ScriptOpBinary op) {
  switch (op) {
  case ScriptOpBinary_Equal:
    return script_bool(script_val_equal(a, b));
  case ScriptOpBinary_NotEqual:
    return script_bool(!script_val_equal(a, b));
  case ScriptOpBinary_Less:
    return script_bool(script_val_less(a, b));
  case ScriptOpBinary_LessOrEqual:
    return script_bool(!script_val_greater(a, b));
  case ScriptOpBinary_Greater:
    return script_bool(script_val_greater(a, b));
  case ScriptOpBinary_GreaterOrEqual:
    return script_bool(!script_val_less(a, b));
  case ScriptOpBinary_Add:
    return script_val_add(a, b);
  case ScriptOpBinary_Sub:
    return script_val_sub(a, b);
  case ScriptOpBinary_Count:
    break;
  }
  diag_assert_fail("Invalid binary operation");
  UNREACHABLE
}

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
