#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_optimize.h"
#include "script_read.h"

#include "utils_internal.h"

spec(optimize) {
  it("can perform basic optimizations") {
    static const struct {
      String input, expect;
    } g_testData[] = {
        // Static pre-evaluation.
        {string_static("1 + 2"), string_static("[value: 3]")},
        {string_static("1 + 2 * 3 + 4"), string_static("[value: 11]")},
        {string_static("vec3(1,2,3)"), string_static("[value: 1, 2, 3]")},

        // Null-coalescing memory stores.
        {
            string_static("$a = $a ?? 42"),
            string_static("[intrinsic: null-coalescing]\n"
                          "  [mem-load: $3645546703]\n"
                          "  [mem-store: $3645546703]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$a ?\?= 42"),
            string_static("[intrinsic: null-coalescing]\n"
                          "  [mem-load: $3645546703]\n"
                          "  [mem-store: $3645546703]\n"
                          "    [value: 42]"),
        },
    };

    ScriptDoc* doc = script_create(g_allocHeap);

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptExpr expr = script_read(doc, null, g_testData[i].input, null, null);
      if (!sentinel_check(expr)) {
        expr = script_optimize(doc, expr);
      }
      check_require_msg(!sentinel_check(expr), "Read failed [{}]", fmt_text(g_testData[i].input));
      check_expr_str(doc, expr, g_testData[i].expect);
    }

    script_destroy(doc);
  }
}
