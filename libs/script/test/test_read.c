#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_error.h"
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
        {string_static("\"Hello World\""), string_static("[value: \"Hello World\"]")},
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
            string_static("[intrinsic: distance]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("distance(1)"),
            string_static("[intrinsic: magnitude]\n"
                          "  [value: 1]"),
        },
        {
            string_static("distance(1 + 2, 3 / 4)"),
            string_static("[intrinsic: distance]\n"
                          "  [intrinsic: add]\n"
                          "    [value: 1]\n"
                          "    [value: 2]\n"
                          "  [intrinsic: div]\n"
                          "    [value: 3]\n"
                          "    [value: 4]"),
        },
        {
            string_static("vector(1, 2, 3)"),
            string_static("[intrinsic: compose-vector3]\n"
                          "  [value: 1]\n"
                          "  [value: 2]\n"
                          "  [value: 3]"),
        },
        {
            string_static("normalize(1)"),
            string_static("[intrinsic: normalize]\n"
                          "  [value: 1]"),
        },
        {
            string_static("angle(1, 2)"),
            string_static("[intrinsic: angle]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("vector_x(1)"),
            string_static("[intrinsic: vector-x]\n"
                          "  [value: 1]"),
        },
        {
            string_static("vector_y(1)"),
            string_static("[intrinsic: vector-y]\n"
                          "  [value: 1]"),
        },
        {
            string_static("vector_z(1)"),
            string_static("[intrinsic: vector-z]\n"
                          "  [value: 1]"),
        },

        // Parenthesized expressions.
        {string_static("(42.1337)"), string_static("[value: 42.1337]")},
        {string_static("($hello)"), string_static("[mem-load: $3944927369]")},
        {string_static("((42.1337))"), string_static("[value: 42.1337]")},
        {string_static("(($hello))"), string_static("[mem-load: $3944927369]")},

        // If expressions.
        {
            string_static("if(true) 2"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [value: 2]\n"
                          "  [value: null]"),
        },
        {
            string_static("if(true) 2 else 3"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [value: 2]\n"
                          "  [value: 3]"),
        },
        {
            string_static("if(true) {2} else {3}"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [value: 2]\n"
                          "  [value: 3]"),
        },
        {
            string_static("if(true) {} else {}"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [value: null]"),
        },
        {
            string_static("if(false) 2 else if(true) 3"),
            string_static("[intrinsic: if]\n"
                          "  [value: false]\n"
                          "  [value: 2]\n"
                          "  [intrinsic: if]\n"
                          "    [value: true]\n"
                          "    [value: 3]\n"
                          "    [value: null]"),
        },
        {
            string_static("if(var i = 42) { i } else { i }"),
            string_static("[intrinsic: if]\n"
                          "  [var-store: 0]\n"
                          "    [value: 42]\n"
                          "  [var-load: 0]\n"
                          "  [var-load: 0]"),
        },
        {
            string_static("if(var i = 1) i; if(var i = 2) i"),
            string_static("[block]\n"
                          "  [intrinsic: if]\n"
                          "    [var-store: 0]\n"
                          "      [value: 1]\n"
                          "    [var-load: 0]\n"
                          "    [value: null]\n"
                          "  [intrinsic: if]\n"
                          "    [var-store: 0]\n"
                          "      [value: 2]\n"
                          "    [var-load: 0]\n"
                          "    [value: null]"),
        },

        // Unary expressions.
        {
            string_static("-42"),
            string_static("[intrinsic: negate]\n"
                          "  [value: 42]"),
        },
        {
            string_static("!true"),
            string_static("[intrinsic: invert]\n"
                          "  [value: true]"),
        },

        // Binary expressions.
        {
            string_static("null == 42"),
            string_static("[intrinsic: equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null != 42"),
            string_static("[intrinsic: not-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("$hello != null"),
            string_static("[intrinsic: not-equal]\n"
                          "  [mem-load: $3944927369]\n"
                          "  [value: null]"),
        },
        {
            string_static("null < 42"),
            string_static("[intrinsic: less]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null <= 42"),
            string_static("[intrinsic: less-or-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null > 42"),
            string_static("[intrinsic: greater]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null >= 42"),
            string_static("[intrinsic: greater-or-equal]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null + 42"),
            string_static("[intrinsic: add]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null - 42"),
            string_static("[intrinsic: sub]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null * 42"),
            string_static("[intrinsic: mul]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null / 42"),
            string_static("[intrinsic: div]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("null % 42"),
            string_static("[intrinsic: mod]\n"
                          "  [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("true && false"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [value: false]\n"
                          "  [value: false]"),
        },
        {
            string_static("true && 2 * 4"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [intrinsic: mul]\n"
                          "    [value: 2]\n"
                          "    [value: 4]\n"
                          "  [value: false]"),
        },
        {
            string_static("true || false"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [value: true]\n"
                          "  [value: false]"),
        },
        {
            string_static("true || 2 * 4"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [value: true]\n"
                          "  [intrinsic: mul]\n"
                          "    [value: 2]\n"
                          "    [value: 4]"),
        },
        {
            string_static("null ?? true"),
            string_static("[intrinsic: null-coalescing]\n"
                          "  [value: null]\n"
                          "  [value: true]"),
        },

        // Ternary expressions.
        {
            string_static("true ? 1 : 2"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("1 > 2 ? 1 + 2 : 3 + 4"),
            string_static("[intrinsic: if]\n"
                          "  [intrinsic: greater]\n"
                          "    [value: 1]\n"
                          "    [value: 2]\n"
                          "  [intrinsic: add]\n"
                          "    [value: 1]\n"
                          "    [value: 2]\n"
                          "  [intrinsic: add]\n"
                          "    [value: 3]\n"
                          "    [value: 4]"),
        },

        // Variable modify expressions.
        {
            string_static("var a; a += 42"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: null]\n"
                          "  [var-store: 0]\n"
                          "    [intrinsic: add]\n"
                          "      [var-load: 0]\n"
                          "      [value: 42]"),
        },
        {
            string_static("var a; a -= 42"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: null]\n"
                          "  [var-store: 0]\n"
                          "    [intrinsic: sub]\n"
                          "      [var-load: 0]\n"
                          "      [value: 42]"),
        },
        {
            string_static("var a; a *= 42"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: null]\n"
                          "  [var-store: 0]\n"
                          "    [intrinsic: mul]\n"
                          "      [var-load: 0]\n"
                          "      [value: 42]"),
        },
        {
            string_static("var a; a /= 42"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: null]\n"
                          "  [var-store: 0]\n"
                          "    [intrinsic: div]\n"
                          "      [var-load: 0]\n"
                          "      [value: 42]"),
        },
        {
            string_static("var a; a %= 42"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: null]\n"
                          "  [var-store: 0]\n"
                          "    [intrinsic: mod]\n"
                          "      [var-load: 0]\n"
                          "      [value: 42]"),
        },
        {
            string_static("var a; a ?\?= 42"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: null]\n"
                          "  [var-store: 0]\n"
                          "    [intrinsic: null-coalescing]\n"
                          "      [var-load: 0]\n"
                          "      [value: 42]"),
        },

        // Memory modify expressions.
        {
            string_static("$hello += 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [intrinsic: add]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello -= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [intrinsic: sub]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello *= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [intrinsic: mul]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello /= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [intrinsic: div]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello %= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [intrinsic: mod]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },
        {
            string_static("$hello ?\?= 42"),
            string_static("[mem-store: $3944927369]\n"
                          "  [intrinsic: null-coalescing]\n"
                          "    [mem-load: $3944927369]\n"
                          "    [value: 42]"),
        },

        // Compound expressions.
        {
            string_static("-42 + 1"),
            string_static("[intrinsic: add]\n"
                          "  [intrinsic: negate]\n"
                          "    [value: 42]\n"
                          "  [value: 1]"),
        },
        {
            string_static("--42"),
            string_static("[intrinsic: negate]\n"
                          "  [intrinsic: negate]\n"
                          "    [value: 42]"),
        },
        {
            string_static("---42"),
            string_static("[intrinsic: negate]\n"
                          "  [intrinsic: negate]\n"
                          "    [intrinsic: negate]\n"
                          "      [value: 42]"),
        },
        {
            string_static("-(42 + 1)"),
            string_static("[intrinsic: negate]\n"
                          "  [intrinsic: add]\n"
                          "    [value: 42]\n"
                          "    [value: 1]"),
        },
        {
            string_static("1 != 42 > 2"),
            string_static("[intrinsic: not-equal]\n"
                          "  [value: 1]\n"
                          "  [intrinsic: greater]\n"
                          "    [value: 42]\n"
                          "    [value: 2]"),
        },
        {
            string_static("null != 1 + 2 + 3"),
            string_static("[intrinsic: not-equal]\n"
                          "  [value: null]\n"
                          "  [intrinsic: add]\n"
                          "    [intrinsic: add]\n"
                          "      [value: 1]\n"
                          "      [value: 2]\n"
                          "    [value: 3]"),
        },
        {
            string_static("(null != 1) + 2 + 3"),
            string_static("[intrinsic: add]\n"
                          "  [intrinsic: add]\n"
                          "    [intrinsic: not-equal]\n"
                          "      [value: null]\n"
                          "      [value: 1]\n"
                          "    [value: 2]\n"
                          "  [value: 3]"),
        },
        {
            string_static("1 != (42 > 2)"),
            string_static("[intrinsic: not-equal]\n"
                          "  [value: 1]\n"
                          "  [intrinsic: greater]\n"
                          "    [value: 42]\n"
                          "    [value: 2]"),
        },
        {
            string_static("(1 != 42) > 2"),
            string_static("[intrinsic: greater]\n"
                          "  [intrinsic: not-equal]\n"
                          "    [value: 1]\n"
                          "    [value: 42]\n"
                          "  [value: 2]"),
        },
        {
            string_static("$hello = 1 + 2"),
            string_static("[mem-store: $3944927369]\n"
                          "  [intrinsic: add]\n"
                          "    [value: 1]\n"
                          "    [value: 2]"),
        },
        {
            string_static("1 * 2 + 2 / 4"),
            string_static("[intrinsic: add]\n"
                          "  [intrinsic: mul]\n"
                          "    [value: 1]\n"
                          "    [value: 2]\n"
                          "  [intrinsic: div]\n"
                          "    [value: 2]\n"
                          "    [value: 4]"),
        },
        {
            string_static("$hello = $world = 1 + 2"),
            string_static("[mem-store: $3944927369]\n"
                          "  [mem-store: $4293346878]\n"
                          "    [intrinsic: add]\n"
                          "      [value: 1]\n"
                          "      [value: 2]"),
        },
        {
            string_static("true || {$a = 1; false}; $a"),
            string_static("[block]\n"
                          "  [intrinsic: if]\n"
                          "    [value: true]\n"
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

        // Variables.
        {
            string_static("var a"),
            string_static("[var-store: 0]\n"
                          "  [value: null]"),
        },
        {
            string_static("var a; a = 42"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: null]\n"
                          "  [var-store: 0]\n"
                          "    [value: 42]"),
        },
        {
            string_static("var a; a"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: null]\n"
                          "  [var-load: 0]"),
        },
        {
            string_static("var a = 42"),
            string_static("[var-store: 0]\n"
                          "  [value: 42]"),
        },
        {
            string_static("var a = 1; var b = 2; var c = 3; var d = 4"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: 1]\n"
                          "  [var-store: 1]\n"
                          "    [value: 2]\n"
                          "  [var-store: 2]\n"
                          "    [value: 3]\n"
                          "  [var-store: 3]\n"
                          "    [value: 4]"),
        },
        {
            string_static("{var a = 1}; {var b = 2}; {var c = 3}; {var d = 4}"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: 1]\n"
                          "  [var-store: 0]\n"
                          "    [value: 2]\n"
                          "  [var-store: 0]\n"
                          "    [value: 3]\n"
                          "  [var-store: 0]\n"
                          "    [value: 4]"),
        },
        {
            string_static("{var a = 1}; {var a = 2}; {var a = 3}; {var a = 4}"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: 1]\n"
                          "  [var-store: 0]\n"
                          "    [value: 2]\n"
                          "  [var-store: 0]\n"
                          "    [value: 3]\n"
                          "  [var-store: 0]\n"
                          "    [value: 4]"),
        },
        {
            string_static("var a = 42; {a}"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: 42]\n"
                          "  [var-load: 0]"),
        },
        {
            string_static("var a = 42; {a * a}"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: 42]\n"
                          "  [intrinsic: mul]\n"
                          "    [var-load: 0]\n"
                          "    [var-load: 0]"),
        },
        {
            string_static("var a = 1; { var b = 2; { var c = 3; a; b; c; } }"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: 1]\n"
                          "  [block]\n"
                          "    [var-store: 1]\n"
                          "      [value: 2]\n"
                          "    [block]\n"
                          "      [var-store: 2]\n"
                          "        [value: 3]\n"
                          "      [var-load: 0]\n"
                          "      [var-load: 1]\n"
                          "      [var-load: 2]"),
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
        {string_static("hello"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("<"), ScriptError_InvalidPrimaryExpression},
        {string_static("1 &&"), ScriptError_MissingPrimaryExpression},
        {string_static("1 ||"), ScriptError_MissingPrimaryExpression},
        {string_static("1 <"), ScriptError_MissingPrimaryExpression},
        {string_static("1 < hello"), ScriptError_NoVariableFoundForIdentifier},
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
        {string_static("1 ? foo"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("1 ? 1 : foo"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("distance"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("distance("), ScriptError_UnterminatedArgumentList},
        {string_static("distance(,"), ScriptError_InvalidPrimaryExpression},
        {string_static("distance(1 2"), ScriptError_UnterminatedArgumentList},
        {string_static("distance(1,"), ScriptError_MissingPrimaryExpression},
        {string_static("distance(1,2,3)"), ScriptError_IncorrectArgumentCountForBuiltinFunction},
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
        {string_static("if"), ScriptError_InvalidConditionCountForIf},
        {string_static("if("), ScriptError_UnterminatedArgumentList},
        {string_static("if()"), ScriptError_InvalidConditionCountForIf},
        {string_static("if(1,2)"), ScriptError_InvalidConditionCountForIf},
        {string_static("if(1)"), ScriptError_MissingPrimaryExpression},
        {string_static("if(1) 1 else"), ScriptError_MissingPrimaryExpression},
        {string_static("if(1) 1; 2 else 3"), ScriptError_UnexpectedTokenAfterExpression},
        {string_static("if(1) var i = 42 else i"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("if(1) 2; else 2;"), ScriptError_InvalidPrimaryExpression},
        {string_static("if(var i = 42) {}; i"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("1 ? var i = 42 : i"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("false && var i = 42; i"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("true || var i = 42; i"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("1 ?? var i = 42; i"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("$test \?\?= var i = 42; i"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("var a; a \?\?= var i = 42; i"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("var i; { var i = 99 }"), ScriptError_VariableIdentifierConflicts},
        {string_static("var"), ScriptError_VariableIdentifierMissing},
        {string_static("var pi"), ScriptError_VariableIdentifierConflicts},
        {string_static("var random"), ScriptError_VariableIdentifierConflicts},
        {string_static("var a; var a"), ScriptError_VariableIdentifierConflicts},
        {string_static("var a ="), ScriptError_MissingPrimaryExpression},
        {string_static("var a = a"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("b ="), ScriptError_NoVariableFoundForIdentifier},
        {string_static("var b; b ="), ScriptError_MissingPrimaryExpression},
        {string_static("a"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("{var a}; a"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("a += 1"), ScriptError_NoVariableFoundForIdentifier},
        {string_static("var a; a +="), ScriptError_MissingPrimaryExpression},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptReadResult res;
      script_read(doc, g_testData[i].input, &res);

      check_require_msg(
          res.type == ScriptResult_Fail, "Read succeeded [{}]", fmt_text(g_testData[i].input));
      check_msg(
          res.error == g_testData[i].expected,
          "{} == {} [{}]",
          script_error_fmt(res.error),
          script_error_fmt(g_testData[i].expected),
          fmt_text(g_testData[i].input));
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

  it("fails when using too many variables") {
    DynString str = dynstring_create(g_alloc_scratch, 1024);
    for (u32 i = 0; i != (script_var_count + 1); ++i) {
      dynstring_append(&str, fmt_write_scratch("var v{} = 42;", fmt_int(i)));
    }

    ScriptReadResult res;
    script_read(doc, dynstring_view(&str), &res);

    check_require(res.type == ScriptResult_Fail);
    check_eq_int(res.error, ScriptError_VariableLimitExceeded);

    dynstring_destroy(&str);
  }

  it("reports error source positions") {
    static const struct {
      String input;
      u16    startLine, startCol;
      u32    endLine, endCol;
    } g_testData[] = {
        {string_static("test"), .startLine = 1, .startCol = 1, .endLine = 1, .endCol = 5},
        {string_static(" \n test "), .startLine = 2, .startCol = 2, .endLine = 2, .endCol = 6},
        {string_static("// Test\n test"), .startLine = 2, .startCol = 2, .endLine = 2, .endCol = 6},
        {string_static(" 你好世界 "), .startLine = 1, .startCol = 2, .endLine = 1, .endCol = 6},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptReadResult res;
      script_read(doc, g_testData[i].input, &res);

      check_require(res.type == ScriptResult_Fail);
      check_eq_int(res.errorStart.line, g_testData[i].startLine);
      check_eq_int(res.errorStart.column, g_testData[i].startCol);
      check_eq_int(res.errorEnd.line, g_testData[i].endLine);
      check_eq_int(res.errorEnd.column, g_testData[i].endCol);
    }
  }

  teardown() { script_destroy(doc); }
}
