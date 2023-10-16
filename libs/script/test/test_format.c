#include "check_spec.h"
#include "script_format.h"

static void format_check(CheckTestContext* _testCtx, const String input, const String expected) {
  Mem       buffer    = mem_stack(1024);
  DynString bufferStr = dynstring_create_over(buffer);

  script_format(&bufferStr, input);

  check_eq_string(dynstring_view(&bufferStr), expected);
}

spec(format) {
  it("can format an empty document") { format_check(_testCtx, string_empty, string_empty); }
}
