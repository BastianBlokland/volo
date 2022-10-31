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

        // Parenthesized expressions.
        {string_static("(42.1337)"), string_static("[value: 42.1337]")},
        {string_static("($hello)"), string_static("[load: $3944927369]")},
        {string_static("((42.1337))"), string_static("[value: 42.1337]")},
        {string_static("(($hello))"), string_static("[load: $3944927369]")},

        // Binary expressions.
        {
            string_static("null == 42"),
            string_static("[op-bin: equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null != 42"),
            string_static("[op-bin: not-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null < 42"),
            string_static("[op-bin: less]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null <= 42"),
            string_static("[op-bin: less-or-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null > 42"),
            string_static("[op-bin: greater]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null >= 42"),
            string_static("[op-bin: greater-or-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },

        // Compound expressions.
        {
            string_static("1 != 42 > 2"),
            string_static("[op-bin: not-equal]\n"
                          "  [value: 1]\n"
                          "  [op-bin: greater]\n"
                          "    [value: 42]\n"
                          "    [value: 2]"),
        },
        {
            string_static("1 != (42 > 2)"),
            string_static("[op-bin: not-equal]\n"
                          "  [value: 1]\n"
                          "  [op-bin: greater]\n"
                          "    [value: 42]\n"
                          "    [value: 2]"),
        },
        {
            string_static("(1 != 42) > 2"),
            string_static("[op-bin: greater]\n"
                          "  [op-bin: not-equal]\n"
                          "    [value: 1]\n"
                          "    [value: 42]\n"
                          "  [value: 2]"),
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

  it("fails when parsing invalid expressions") {
    const static struct {
      String      input;
      ScriptError expected;
    } g_testData[] = {
        {string_static(""), ScriptError_MissingPrimaryExpression},
        {string_static("<"), ScriptError_InvalidPrimaryExpression},
        {string_static(")"), ScriptError_InvalidPrimaryExpression},
        {string_static("("), ScriptError_MissingPrimaryExpression},
        {string_static("(1"), ScriptError_UnclosedParenthesizedExpression},
        {string_static("(1 1"), ScriptError_UnclosedParenthesizedExpression},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptReadResult res;
      const String     remInput = script_read_expr(doc, g_testData[i].input, &res);
      check_eq_string(remInput, string_lit(""));

      check_require_msg(res.type == ScriptResult_Fail, "Failed to read: {}", fmt_int(i));
      check_msg(
          res.error == g_testData[i].expected,
          "{} == {}",
          script_error_fmt(res.error),
          script_error_fmt(g_testData[i].expected));
    }
  }

  it("fails when recursing too deep") {
    DynString str = dynstring_create(g_alloc_scratch, 256);
    dynstring_append_chars(&str, '(', 100);

    ScriptReadResult res;
    script_read_expr(doc, dynstring_view(&str), &res);

    check_require(res.type == ScriptResult_Fail);
    check_eq_int(res.error, ScriptError_RecursionLimitExceeded);

    dynstring_destroy(&str);
  }

  teardown() { script_destroy(doc); }
}
