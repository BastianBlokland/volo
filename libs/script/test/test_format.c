#include "check_spec.h"
#include "core_array.h"
#include "script_format.h"

spec(format) {
  Mem       buffer = mem_stack(4096);
  DynString bufferStr;

  setup() { bufferStr = dynstring_create_over(buffer); }

  it("normalizes whitespace in lines") {
    static const struct {
      String input, expect;
    } g_testData[] = {
        {string_static("\n"), string_static("\n")},
        {string_static("42\n"), string_static("42\n")},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      dynstring_clear(&bufferStr);
      script_format(&bufferStr, g_testData[i].input);
      check_eq_string(dynstring_view(&bufferStr), g_testData[i].expect);
    }
  }

  teardown() { dynstring_destroy(&bufferStr); }
}
