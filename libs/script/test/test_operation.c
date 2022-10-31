#include "check_spec.h"
#include "script_operation.h"

#include "utils_internal.h"

spec(operation) {
  it("can perform unary operations") {
    check_eq_val(script_op_unary(script_number(42), ScriptOpUnary_Negate), script_number(-42));
  }

  it("can perform binary operations") {
    check_truthy(script_op_binary(script_number(1), script_number(1), ScriptOpBinary_Equal));
    check_truthy(script_op_binary(script_number(2), script_number(1), ScriptOpBinary_NotEqual));
    check_truthy(script_op_binary(script_number(1), script_number(2), ScriptOpBinary_Less));
    check_truthy(script_op_binary(script_number(2), script_number(2), ScriptOpBinary_LessOrEqual));
    check_truthy(script_op_binary(script_number(2), script_number(1), ScriptOpBinary_Greater));
    check_truthy(
        script_op_binary(script_number(2), script_number(2), ScriptOpBinary_GreaterOrEqual));

    check_falsy(script_op_binary(script_number(2), script_number(1), ScriptOpBinary_Equal));
    check_falsy(script_op_binary(script_number(1), script_number(1), ScriptOpBinary_NotEqual));
    check_falsy(script_op_binary(script_number(2), script_number(1), ScriptOpBinary_Less));
    check_falsy(script_op_binary(script_number(2), script_number(1), ScriptOpBinary_LessOrEqual));
    check_falsy(script_op_binary(script_number(1), script_number(2), ScriptOpBinary_Greater));
    check_falsy(
        script_op_binary(script_number(1), script_number(2), ScriptOpBinary_GreaterOrEqual));

    check_eq_val(
        script_op_binary(script_number(1), script_number(2), ScriptOpBinary_Add), script_number(3));
    check_eq_val(
        script_op_binary(script_number(1), script_number(2), ScriptOpBinary_Sub),
        script_number(-1));
  }
}
