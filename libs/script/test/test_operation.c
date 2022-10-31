#include "check_spec.h"
#include "script_operation.h"

#include "utils_internal.h"

spec(operation) {
  it("can perform binary operations") {
    check_truthy(script_op_bin(script_number(1), script_number(1), ScriptOpBin_Equal));
    check_truthy(script_op_bin(script_number(2), script_number(1), ScriptOpBin_NotEqual));
    check_truthy(script_op_bin(script_number(1), script_number(2), ScriptOpBin_Less));
    check_truthy(script_op_bin(script_number(2), script_number(2), ScriptOpBin_LessOrEqual));
    check_truthy(script_op_bin(script_number(2), script_number(1), ScriptOpBin_Greater));
    check_truthy(script_op_bin(script_number(2), script_number(2), ScriptOpBin_GreaterOrEqual));

    check_falsy(script_op_bin(script_number(2), script_number(1), ScriptOpBin_Equal));
    check_falsy(script_op_bin(script_number(1), script_number(1), ScriptOpBin_NotEqual));
    check_falsy(script_op_bin(script_number(2), script_number(1), ScriptOpBin_Less));
    check_falsy(script_op_bin(script_number(2), script_number(1), ScriptOpBin_LessOrEqual));
    check_falsy(script_op_bin(script_number(1), script_number(2), ScriptOpBin_Greater));
    check_falsy(script_op_bin(script_number(1), script_number(2), ScriptOpBin_GreaterOrEqual));

    check_eq_val(
        script_op_bin(script_number(1), script_number(2), ScriptOpBin_Add), script_number(3));
    check_eq_val(
        script_op_bin(script_number(1), script_number(2), ScriptOpBin_Sub), script_number(-1));
  }
}
