#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_read.h"

#include "utils_internal.h"

spec(read) {
  ScriptDoc* doc = null;

  setup() { doc = script_create(g_alloc_heap); }

  it("can parse expressions") {
    const static struct {
      String input, expect;
    } g_testData[] = {
        // Primary expressions.
        {string_static("null"), string_static("[value: null]")},
        {string_static("42.1337"), string_static("[value: 42.1337]")},
        {string_static("true"), string_static("[value: true]")},
        {string_static("$hello"), string_static("[load: $3944927369]")},

        // Comparisons.
        {
            string_static("null == 42"),
            string_static("[compare: equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null != 42"),
            string_static("[compare: not-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null < 42"),
            string_static("[compare: less]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null <= 42"),
            string_static("[compare: less-or-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null > 42"),
            string_static("[compare: greater]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null >= 42"),
            string_static("[compare: greater-or-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },

        // Compound expressions.
        {
            string_static("1 != 42 > 2"),
            string_static("[compare: not-equal]\n"
                          "  [value: 1]\n"
                          "  [compare: greater]\n"
                          "    [value: 42]\n"
                          "    [value: 2]"),
        },
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptReadResult res;
      const String     remInput = script_read_expr(doc, g_testData[i].input, &res);
      check_eq_string(remInput, string_lit(""));

      check_require_msg(res.type == ScriptResult_Success, "Failed to read: {}", fmt_int(i));
      check_expr_str(doc, res.expr, g_testData[i].expect);
    }
  }

  teardown() { script_destroy(doc); }
}
