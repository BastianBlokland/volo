#include "check_spec.h"
#include "script_compare.h"

spec(compare) {
  it("can compare values") {
    check(script_compare(script_number(1), script_number(1), ScriptComparison_Equal));
    check(script_compare(script_number(2), script_number(1), ScriptComparison_NotEqual));
    check(script_compare(script_number(1), script_number(2), ScriptComparison_Less));
    check(script_compare(script_number(2), script_number(2), ScriptComparison_LessOrEqual));
    check(script_compare(script_number(2), script_number(1), ScriptComparison_Greater));
    check(script_compare(script_number(2), script_number(2), ScriptComparison_GreaterOrEqual));

    check(!script_compare(script_number(2), script_number(1), ScriptComparison_Equal));
    check(!script_compare(script_number(1), script_number(1), ScriptComparison_NotEqual));
    check(!script_compare(script_number(2), script_number(1), ScriptComparison_Less));
    check(!script_compare(script_number(2), script_number(1), ScriptComparison_LessOrEqual));
    check(!script_compare(script_number(1), script_number(2), ScriptComparison_Greater));
    check(!script_compare(script_number(1), script_number(2), ScriptComparison_GreaterOrEqual));
  }
}
