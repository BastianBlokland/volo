#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_read.h"

#include "utils_internal.h"

spec(read) {
  ScriptDoc* doc = null;

  setup() { doc = script_create(g_alloc_heap); }

  it("can parse expressions") {
    static const struct {
      String input, expect;
    } g_testData[] = {
        // Primary expressions.
        {string_static(""), string_static("[value: null]")},
        {string_static("null"), string_static("[value: null]")},
        {string_static("42.1337"), string_static("[value: 42.1337]")},
        {string_static("true"), string_static("[value: true]")},
        {string_static("$hello"), string_static("[mem-load: $3944927369]")},
        {string_static("pi"), string_static("[value: 3.1415927]")},
        {string_static("deg_to_rad"), string_static("[value: 0.0174533]")},
        {string_static("rad_to_deg"), string_static("[value: 57.2957802]")},
        {
            string_static("$hello = 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [value: 42]"),
        },
        {
            string_static("$hello = $world"),
            string_static("[mem-store: $3944927369]\n"
                          "  [mem-load: $4293346878]"),
        },
        {
            string_static("distance(1,2)"),
            string_static("[op-binary: distance]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("distance(1)"),
            string_static("[op-unary: magnitude]\n"
                          "  [value: 1]"),
        },
        {
            string_static("distance(1 + 2, 3 / 4)"),
            string_static("[op-binary: distance]\n"
                          "  [op-binary: add]\n"
                          "    [value: 1]\n"
                          "    [value: 2]\n"
                          "  [op-binary: div]\n"
                          "    [value: 3]\n"
                          "    [value: 4]"),
        },
        {
            string_static("vector(1, 2, 3)"),
            string_static("[op-ternary: compose-vector3]\n"
                          "  [value: 1]\n"
                          "  [value: 2]\n"
                          "  [value: 3]"),
        },
        {
            string_static("normalize(1)"),
            string_static("[op-unary: normalize]\n"
                          "  [value: 1]"),
        },
        {
            string_static("angle(1, 2)"),
            string_static("[op-binary: angle]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("vector_x(1)"),
            string_static("[op-unary: vector-x]\n"
                          "  [value: 1]"),
        },
        {
            string_static("vector_y(1)"),
            string_static("[op-unary: vector-y]\n"
                          "  [value: 1]"),
        },
        {
            string_static("vector_z(1)"),
            string_static("[op-unary: vector-z]\n"
                          "  [value: 1]"),
        },

        // Parenthesized expressions.
        {string_static("(42.1337)"), string_static("[value: 42.1337]")},
        {string_static("($hello)"), string_static("[mem-load: $3944927369]")},
        {string_static("((42.1337))"), string_static("[value: 42.1337]")},
        {string_static("(($hello))"), string_static("[mem-load: $3944927369]")},

        // Unary expressions.
        {
            string_static("-42"),
            string_static("[op-unary: negate]\n"
                          "  [value: 42]"),
        },
        {
            string_static("!true"),
            string_static("[op-unary: invert]\n"
                          "  [value: true]"),
        },

        // Binary expressions.
        {
            string_static("null == 42"),
            string_static("[op-binary: equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null != 42"),
            string_static("[op-binary: not-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("$hello != null"),
            string_static("[op-binary: not-equal]\n"
                          "  [mem-load: $3944927369]\n"
                          "  [value: null]"),
        },
        {
            string_static("null < 42"),
            string_static("[op-binary: less]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null <= 42"),
            string_static("[op-binary: less-or-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null > 42"),
            string_static("[op-binary: greater]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null >= 42"),
            string_static("[op-binary: greater-or-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null + 42"),
            string_static("[op-binary: add]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null - 42"),
            string_static("[op-binary: sub]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null * 42"),
            string_static("[op-binary: mul]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null / 42"),
            string_static("[op-binary: div]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null % 42"),
            string_static("[op-binary: mod]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("true && false"),
            string_static("[op-binary: logic-and]\n"
                          "  [value: true]\n"
                          "  [value: false]"),
        },
        {
            string_static("true || false"),
            string_static("[op-binary: logic-or]\n"
                          "  [value: true]\n"
                          "  [value: false]"),
        },
        {
            string_static("null ?? true"),
            string_static("[op-binary: null-coalescing]\n"
                          "  [value: null]\n"
                          "  [value: true]"),
        },

        // Ternary expressions.
        {
            string_static("true ? 1 : 2"),
            string_static("[op-ternary: select]\n"
                          "  [value: true]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("1 > 2 ? 1 + 2 : 3 + 4"),
            string_static("[op-ternary: select]\n"
                          "  [op-binary: greater]\n"
                          "    [value: 1]\n"
                          "    [value: 2]\n"
                          "  [op-binary: add]\n"
                          "    [value: 1]\n"
                          "    [value: 2]\n"
                          "  [op-binary: add]\n"
                          "    [value: 3]\n"
                          "    [value: 4]"),
        },

        // Modify expressions.
        {
            string_static("$hello += 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [op-binary: add]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello -= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [op-binary: sub]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello *= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [op-binary: mul]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello /= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [op-binary: div]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello %= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [op-binary: mod]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello ?\?= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [op-binary: null-coalescing]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },

        // Compound expressions.
        {
            string_static("-42 + 1"),
            string_static("[op-binary: add]\n"
                          "  [op-unary: negate]\n"
                          "    [value: 42]\n"
                          "  [value: 1]"),
        },
        {
            string_static("--42"),
            string_static("[op-unary: negate]\n"
                          "  [op-unary: negate]\n"
                          "    [value: 42]"),
        },
        {
            string_static("---42"),
            string_static("[op-unary: negate]\n"
                          "  [op-unary: negate]\n"
                          "    [op-unary: negate]\n"
                          "      [value: 42]"),
        },
        {
            string_static("-(42 + 1)"),
            string_static("[op-unary: negate]\n"
                          "  [op-binary: add]\n"
                          "    [value: 42]\n"
                          "    [value: 1]"),
        },
        {
            string_static("1 != 42 > 2"),
            string_static("[op-binary: not-equal]\n"
                          "  [value: 1]\n"
                          "  [op-binary: greater]\n"
                          "    [value: 42]\n"
                          "    [value: 2]"),
        },
        {
            string_static("null != 1 + 2 + 3"),
            string_static("[op-binary: not-equal]\n"
                          "  [value: null]\n"
                          "  [op-binary: add]\n"
                          "    [op-binary: add]\n"
                          "      [value: 1]\n"
                          "      [value: 2]\n"
                          "    [value: 3]"),
        },
        {
            string_static("(null != 1) + 2 + 3"),
            string_static("[op-binary: add]\n"
                          "  [op-binary: add]\n"
                          "    [op-binary: not-equal]\n"
                          "      [value: null]\n"
                          "      [value: 1]\n"
                          "    [value: 2]\n"
                          "  [value: 3]"),
        },
        {
            string_static("1 != (42 > 2)"),
            string_static("[op-binary: not-equal]\n"
                          "  [value: 1]\n"
                          "  [op-binary: greater]\n"
                          "    [value: 42]\n"
                          "    [value: 2]"),
        },
        {
            string_static("(1 != 42) > 2"),
            string_static("[op-binary: greater]\n"
                          "  [op-binary: not-equal]\n"
                          "    [value: 1]\n"
                          "    [value: 42]\n"
                          "  [value: 2]"),
        },
        {
            string_static("$hello = 1 + 2"),
            string_static("[mem-store: $3944927369]\n"
                          "  [op-binary: add]\n"
                          "    [value: 1]\n"
                          "    [value: 2]"),
        },
        {
            string_static("1 * 2 + 2 / 4"),
            string_static("[op-binary: add]\n"
                          "  [op-binary: mul]\n"
                          "    [value: 1]\n"
                          "    [value: 2]\n"
                          "  [op-binary: div]\n"
                          "    [value: 2]\n"
                          "    [value: 4]"),
        },
        {
            string_static("$hello = $world = 1 + 2"),
            string_static("[mem-store: $3944927369]\n"
                          "  [mem-store: $4293346878]\n"
                          "    [op-binary: add]\n"
                          "      [value: 1]\n"
                          "      [value: 2]"),
        },
        {
            string_static("true || {$a = 1; false}; $a"),
            string_static("[block]\n"
                          "  [op-binary: logic-or]\n"
                          "    [value: true]\n"
                          "    [block]\n"
                          "      [mem-store: $3645546703]\n"
                          "        [value: 1]\n"
                          "      [value: false]\n"
                          "  [mem-load: $3645546703]"),
        },

        // Group expressions.
        {
            string_static("1; 2"),
            string_static("[block]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("1; 2;"),
            string_static("[block]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("1; 2;\t \n"),
            string_static("[block]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("1; 2; 3; 4; 5"),
            string_static("[block]\n"
                          "  [value: 1]\n"
                          "  [value: 2]\n"
                          "  [value: 3]\n"
                          "  [value: 4]\n"
                          "  [value: 5]"),
        },
        {
            string_static("$a = 1; $b = 2; $c = 3"),
            string_static("[block]\n"
                          "  [mem-store: $3645546703]\n"
                          "    [value: 1]\n"
                          "  [mem-store: $1612769824]\n"
                          "    [value: 2]\n"
                          "  [mem-store: $1857025631]\n"
                          "    [value: 3]"),
        },
        {
            string_static("{1}"),
            string_static("[value: 1]"),
        },
        {
            string_static("{1; 2}"),
            string_static("[block]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("{1; 2;}"),
            string_static("[block]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptReadResult res;
      script_read(doc, g_testData[i].input, &res);

      check_require_msg(res.type == ScriptResult_Success, "Failed to read: {}", fmt_int(i));
      check_expr_str(doc, res.expr, g_testData[i].expect);
    }
  }

  it("fails when parsing invalid expressions") {
    static const struct {
      String      input;
      ScriptError expected;
    } g_testData[] = {
        {string_static("hello"), ScriptError_NoConstantFoundForIdentifier},
        {string_static("<"), ScriptError_InvalidPrimaryExpression},
        {string_static("1 <"), ScriptError_MissingPrimaryExpression},
        {string_static("1 < hello"), ScriptError_NoConstantFoundForIdentifier},
        {string_static(")"), ScriptError_InvalidPrimaryExpression},
        {string_static("("), ScriptError_MissingPrimaryExpression},
        {string_static("(1"), ScriptError_UnclosedParenthesizedExpression},
        {string_static("(1 1"), ScriptError_UnclosedParenthesizedExpression},
        {string_static("!"), ScriptError_MissingPrimaryExpression},
        {string_static(";"), ScriptError_InvalidPrimaryExpression},
        {string_static("1 ; ;"), ScriptError_InvalidPrimaryExpression},
        {string_static("1;;"), ScriptError_InvalidPrimaryExpression},
        {string_static("?"), ScriptError_InvalidPrimaryExpression},
        {string_static("1?"), ScriptError_MissingPrimaryExpression},
        {string_static("1 ?"), ScriptError_MissingPrimaryExpression},
        {string_static("1?1"), ScriptError_MissingColonInSelectExpression},
        {string_static("1 ? 1"), ScriptError_MissingColonInSelectExpression},
        {string_static("1 ? foo"), ScriptError_NoConstantFoundForIdentifier},
        {string_static("1 ? 1 : foo"), ScriptError_NoConstantFoundForIdentifier},
        {string_static("distance"), ScriptError_NoConstantFoundForIdentifier},
        {string_static("distance("), ScriptError_UnterminatedArgumentList},
        {string_static("distance(,"), ScriptError_InvalidPrimaryExpression},
        {string_static("distance(1 2"), ScriptError_UnterminatedArgumentList},
        {string_static("distance(1,"), ScriptError_MissingPrimaryExpression},
        {string_static("hello()"), ScriptError_NoFunctionFoundForIdentifier},
        {string_static("hello(null)"), ScriptError_NoFunctionFoundForIdentifier},
        {string_static("hello(1,2,3,4,5)"), ScriptError_NoFunctionFoundForIdentifier},
        {string_static("hello(1 + 2 + 4, 5 + 6 + 7)"), ScriptError_NoFunctionFoundForIdentifier},
        {string_static("hello(1,2,3,4,5,6,7,8,9,10)"), ScriptError_NoFunctionFoundForIdentifier},
        {string_static("hello(1,2,3,4,5,6,7,8,9,10,"), ScriptError_ArgumentCountExceedsMaximum},
        {string_static("{"), ScriptError_UnterminatedScope},
        {string_static("{1"), ScriptError_UnterminatedScope},
        {string_static("{1;"), ScriptError_UnterminatedScope},
        {string_static("{1;2"), ScriptError_UnterminatedScope},
        {string_static("{1;2;"), ScriptError_UnterminatedScope},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptReadResult res;
      script_read(doc, g_testData[i].input, &res);

      check_require_msg(res.type == ScriptResult_Fail, "Read succeeded (index: {})", fmt_int(i));
      check_msg(
          res.error == g_testData[i].expected,
          "{} == {} (index: {})",
          script_error_fmt(res.error),
          script_error_fmt(g_testData[i].expected),
          fmt_int(i));
    }
  }

  it("can read all input") {
    ScriptReadResult res;
    script_read(doc, string_lit("1  "), &res);

    check_require(res.type == ScriptResult_Success);
  }

  it("fails when read-all finds additional tokens after the expression") {
    ScriptReadResult res;
    script_read(doc, string_lit("1 1"), &res);

    check_require(res.type == ScriptResult_Fail);
    check(res.error == ScriptError_UnexpectedTokenAfterExpression);
  }

  it("fails when recursing too deep") {
    DynString str = dynstring_create(g_alloc_scratch, 256);
    dynstring_append_chars(&str, '(', 100);

    ScriptReadResult res;
    script_read(doc, dynstring_view(&str), &res);

    check_require(res.type == ScriptResult_Fail);
    check_eq_int(res.error, ScriptError_RecursionLimitExceeded);

    dynstring_destroy(&str);
  }

  teardown() { script_destroy(doc); }
}
