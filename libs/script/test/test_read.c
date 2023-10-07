#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_binder.h"
#include "script_read.h"
#include "script_result.h"

#include "utils_internal.h"

spec(read) {
  ScriptDoc*    doc    = null;
  ScriptBinder* binder = null;

  setup() {
    doc = script_create(g_alloc_heap);

    binder = script_binder_create(g_alloc_heap);
    script_binder_declare(binder, string_hash_lit("bind_test_1"), null);
    script_binder_declare(binder, string_hash_lit("bind_test_2"), null);
    script_binder_finalize(binder);
  }

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
            string_static("[intrinsic: vector3-compose]\n"
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
        {
            string_static("euler(1,2,3)"),
            string_static("[intrinsic: quat-from-euler]\n"
                          "  [value: 1]\n"
                          "  [value: 2]\n"
                          "  [value: 3]"),
        },
        {
            string_static("assert(1)"),
            string_static("[intrinsic: assert]\n"
                          "  [value: 1]"),
        },

        // External functions.
        {
            string_static("bind_test_1()"),
            string_static("[extern: 1]"),
        },
        {
            string_static("bind_test_1(1, 2, 3)"),
            string_static("[extern: 1]\n"
                          "  [value: 1]\n"
                          "  [value: 2]\n"
                          "  [value: 3]"),
        },

        // Parenthesized expressions.
        {string_static("(42.1337)"), string_static("[value: 42.1337]")},
        {string_static("($hello)"), string_static("[mem-load: $3944927369]")},
        {string_static("((42.1337))"), string_static("[value: 42.1337]")},
        {string_static("(($hello))"), string_static("[mem-load: $3944927369]")},

        // If expressions.
        {
            string_static("if(true) {2}"),
            string_static("[intrinsic: if]\n"
                          "  [value: true]\n"
                          "  [value: 2]\n"
                          "  [value: null]"),
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
            string_static("if(false) {2} else if(true) {3}"),
            string_static("[intrinsic: if]\n"
                          "  [value: false]\n"
                          "  [value: 2]\n"
                          "  [intrinsic: if]\n"
                          "    [value: true]\n"
                          "    [value: 3]\n"
                          "    [value: null]"),
        },
        {
            string_static("if(false) {2} else if(true) {3} else {4}"),
            string_static("[intrinsic: if]\n"
                          "  [value: false]\n"
                          "  [value: 2]\n"
                          "  [intrinsic: if]\n"
                          "    [value: true]\n"
                          "    [value: 3]\n"
                          "    [value: 4]"),
        },
        {
            string_static("if(var i = 42) {i} else {i}"),
            string_static("[intrinsic: if]\n"
                          "  [var-store: 0]\n"
                          "    [value: 42]\n"
                          "  [var-load: 0]\n"
                          "  [var-load: 0]"),
        },
        {
            string_static("if(var i = 1) {i} if(var i = 2) {i}"),
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
        {
            string_static("if(true) {} var i"),
            string_static("[block]\n"
                          "  [intrinsic: if]\n"
                          "    [value: true]\n"
                          "    [value: null]\n"
                          "    [value: null]\n"
                          "  [var-store: 0]\n"
                          "    [value: null]"),
        },

        // While expressions.
        {
            string_static("var i = 0;"
                          "while(i < 10) {"
                          "  bind_test_1(i);"
                          "  i += 1;"
                          "}"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: 0]\n"
                          "  [intrinsic: while]\n"
                          "    [intrinsic: less]\n"
                          "      [var-load: 0]\n"
                          "      [value: 10]\n"
                          "    [block]\n"
                          "      [extern: 1]\n"
                          "        [var-load: 0]\n"
                          "      [var-store: 0]\n"
                          "        [intrinsic: add]\n"
                          "          [var-load: 0]\n"
                          "          [value: 1]"),
        },
        {
            string_static("while(true) { bind_test_1() }"),
            string_static("[intrinsic: while]\n"
                          "  [value: true]\n"
                          "  [extern: 1]"),
        },
        {
            string_static("while(true) { break }"),
            string_static("[intrinsic: while]\n"
                          "  [value: true]\n"
                          "  [intrinsic: break]"),
        },
        {
            string_static("while(true) { continue }"),
            string_static("[intrinsic: while]\n"
                          "  [value: true]\n"
                          "  [intrinsic: continue]"),
        },

        // For expressions.
        {
            string_static("for(;;) {}"),
            string_static("[intrinsic: for]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [value: null]"),
        },
        {
            string_static("for(;;) { bind_test_1() }"),
            string_static("[intrinsic: for]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [extern: 1]"),
        },
        {
            string_static("for(var i = 0;;) { bind_test_1() }"),
            string_static("[intrinsic: for]\n"
                          "  [var-store: 0]\n"
                          "    [value: 0]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [extern: 1]"),
        },
        {
            string_static("for(;42;) { bind_test_1() }"),
            string_static("[intrinsic: for]\n"
                          "  [value: null]\n"
                          "  [value: 42]\n"
                          "  [value: null]\n"
                          "  [extern: 1]"),
        },
        {
            string_static("for(;;42) { bind_test_1() }"),
            string_static("[intrinsic: for]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: 42]\n"
                          "  [extern: 1]"),
        },
        {
            string_static("for(var i = 0; i != 10;) { bind_test_1() }"),
            string_static("[intrinsic: for]\n"
                          "  [var-store: 0]\n"
                          "    [value: 0]\n"
                          "  [intrinsic: not-equal]\n"
                          "    [var-load: 0]\n"
                          "    [value: 10]\n"
                          "  [value: null]\n"
                          "  [extern: 1]"),
        },
        {
            string_static("for(var i = 0; i != 10; i += 1) { bind_test_1() }"),
            string_static("[intrinsic: for]\n"
                          "  [var-store: 0]\n"
                          "    [value: 0]\n"
                          "  [intrinsic: not-equal]\n"
                          "    [var-load: 0]\n"
                          "    [value: 10]\n"
                          "  [var-store: 0]\n"
                          "    [intrinsic: add]\n"
                          "      [var-load: 0]\n"
                          "      [value: 1]\n"
                          "  [extern: 1]"),
        },
        {
            string_static("for(;;) { break }"),
            string_static("[intrinsic: for]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [intrinsic: break]"),
        },
        {
            string_static("for(;;) { continue }"),
            string_static("[intrinsic: for]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [intrinsic: continue]"),
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
            string_static("[intrinsic: logic-and]\n"
                          "  [value: true]\n"
                          "  [value: false]"),
        },
        {
            string_static("true && 2 * 4"),
            string_static("[intrinsic: logic-and]\n"
                          "  [value: true]\n"
                          "  [intrinsic: mul]\n"
                          "    [value: 2]\n"
                          "    [value: 4]"),
        },
        {
            string_static("true || false"),
            string_static("[intrinsic: logic-or]\n"
                          "  [value: true]\n"
                          "  [value: false]"),
        },
        {
            string_static("true || 2 * 4"),
            string_static("[intrinsic: logic-or]\n"
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
            string_static("[intrinsic: select]\n"
                          "  [value: true]\n"
                          "  [value: 1]\n"
                          "  [value: 2]"),
        },
        {
            string_static("1 > 2 ? 1 + 2 : 3 + 4"),
            string_static("[intrinsic: select]\n"
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
                          "  [intrinsic: logic-or]\n"
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
            string_static("{1;}"),
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
        {
            string_static("{1; 2}"),
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
      script_read(doc, binder, g_testData[i].input, &res);

      check_require_msg(
          res.type == ScriptResult_Success, "Read failed [{}]", fmt_text(g_testData[i].input));
      check_expr_str(doc, res.expr, g_testData[i].expect);
    }
  }

  it("fails when parsing invalid expressions") {
    static const struct {
      String       input;
      ScriptResult expected;
    } g_testData[] = {
        {string_static("}"), ScriptResult_InvalidPrimaryExpression},
        {string_static("1 }"), ScriptResult_MissingSemicolon},
        {string_static("1 1"), ScriptResult_MissingSemicolon},
        {string_static("hello"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("<"), ScriptResult_InvalidPrimaryExpression},
        {string_static("1 &&"), ScriptResult_MissingPrimaryExpression},
        {string_static("1 ||"), ScriptResult_MissingPrimaryExpression},
        {string_static("1 <"), ScriptResult_MissingPrimaryExpression},
        {string_static("1 < hello"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static(")"), ScriptResult_InvalidPrimaryExpression},
        {string_static("("), ScriptResult_MissingPrimaryExpression},
        {string_static("(1"), ScriptResult_UnclosedParenthesizedExpression},
        {string_static("(1 1"), ScriptResult_UnclosedParenthesizedExpression},
        {string_static("!"), ScriptResult_MissingPrimaryExpression},
        {string_static(";"), ScriptResult_ExtraneousSemicolon},
        {string_static("1 ; ;"), ScriptResult_ExtraneousSemicolon},
        {string_static("1;;"), ScriptResult_ExtraneousSemicolon},
        {string_static("?"), ScriptResult_InvalidPrimaryExpression},
        {string_static("1?"), ScriptResult_MissingPrimaryExpression},
        {string_static("1 ?"), ScriptResult_MissingPrimaryExpression},
        {string_static("1?1"), ScriptResult_MissingColonInSelectExpression},
        {string_static("1 ? 1"), ScriptResult_MissingColonInSelectExpression},
        {string_static("1 ? foo"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("1 ? 1 : foo"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("1 ? 1 : 1 2"), ScriptResult_MissingSemicolon},
        {string_static("distance"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("distance("), ScriptResult_UnterminatedArgumentList},
        {string_static("distance(,"), ScriptResult_InvalidPrimaryExpression},
        {string_static("distance(1 2"), ScriptResult_UnterminatedArgumentList},
        {string_static("distance(1,"), ScriptResult_MissingPrimaryExpression},
        {string_static("distance(1,2,3)"), ScriptResult_IncorrectArgumentCountForBuiltinFunction},
        {string_static("hello()"), ScriptResult_NoFunctionFoundForIdentifier},
        {string_static("hello(null)"), ScriptResult_NoFunctionFoundForIdentifier},
        {string_static("hello(1,2,3,4,5)"), ScriptResult_NoFunctionFoundForIdentifier},
        {string_static("hello(1 + 2 + 4, 5 + 6 + 7)"), ScriptResult_NoFunctionFoundForIdentifier},
        {string_static("hello(1,2,3,4,5,6,7,8,9,10)"), ScriptResult_NoFunctionFoundForIdentifier},
        {string_static("hello(1,2,3,4,5,6,7,8,9,10,"), ScriptResult_ArgumentCountExceedsMaximum},
        {string_static("{"), ScriptResult_UnterminatedBlock},
        {string_static("{1"), ScriptResult_UnterminatedBlock},
        {string_static("{1;"), ScriptResult_UnterminatedBlock},
        {string_static("{1;2"), ScriptResult_UnterminatedBlock},
        {string_static("{1;2;"), ScriptResult_UnterminatedBlock},
        {string_static("if"), ScriptResult_InvalidConditionCount},
        {string_static("if("), ScriptResult_UnterminatedArgumentList},
        {string_static("if()"), ScriptResult_InvalidConditionCount},
        {string_static("if(1,2)"), ScriptResult_InvalidConditionCount},
        {string_static("if(1)"), ScriptResult_BlockExpected},
        {string_static("if(1) 1"), ScriptResult_BlockExpected},
        {string_static("if(1) {1} else"), ScriptResult_BlockOrIfExpected},
        {string_static("if(1) {1}; 2 else 3"), ScriptResult_ExtraneousSemicolon},
        {string_static("if(1) {var i = 42} else {i}"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("if(1) {2}; else {2}"), ScriptResult_ExtraneousSemicolon},
        {string_static("if(var i = 42) {} i"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("while"), ScriptResult_InvalidWhileLoop},
        {string_static("while("), ScriptResult_UnterminatedArgumentList},
        {string_static("while()"), ScriptResult_InvalidWhileLoop},
        {string_static("while(1,2)"), ScriptResult_InvalidWhileLoop},
        {string_static("while(1)"), ScriptResult_BlockExpected},
        {string_static("while(1) 1"), ScriptResult_BlockExpected},
        {string_static("while(var i = 42) {} i"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("for"), ScriptResult_InvalidForLoop},
        {string_static("for("), ScriptResult_MissingPrimaryExpression},
        {string_static("for()"), ScriptResult_InvalidPrimaryExpression},
        {string_static("for(1,2)"), ScriptResult_InvalidForLoop},
        {string_static("for(1)"), ScriptResult_InvalidForLoop},
        {string_static("for(1 1) 1"), ScriptResult_InvalidForLoop},
        {string_static("for(1;)"), ScriptResult_InvalidPrimaryExpression},
        {string_static("for(;;;)"), ScriptResult_ExtraneousSemicolon},
        {string_static("for(;;"), ScriptResult_MissingPrimaryExpression},
        {string_static("for(;;1"), ScriptResult_InvalidForLoop},
        {string_static("for(var i = 0;;) 1"), ScriptResult_BlockExpected},
        {string_static("for(var i = 0;;) {} i"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("1 ? var i = 42 : i"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("false && var i = 42; i"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("true || var i = 42; i"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("1 ?? var i = 42; i"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("$test \?\?= var i = 42; i"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("var a; a \?\?= var i = 42; i"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("random"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("bind_test_1"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("var i; { var i = 99 }"), ScriptResult_VariableIdentifierConflicts},
        {string_static("var"), ScriptResult_VariableIdentifierMissing},
        {string_static("var pi"), ScriptResult_VariableIdentifierConflicts},
        {string_static("var a; var a"), ScriptResult_VariableIdentifierConflicts},
        {string_static("var a ="), ScriptResult_MissingPrimaryExpression},
        {string_static("var a = a"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("b ="), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("var b; b ="), ScriptResult_MissingPrimaryExpression},
        {string_static("a"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("{var a}; a"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("a += 1"), ScriptResult_NoVariableFoundForIdentifier},
        {string_static("var a; a +="), ScriptResult_MissingPrimaryExpression},
        {string_static("continue"), ScriptResult_NotValidOutsideLoopBody},
        {string_static("break"), ScriptResult_NotValidOutsideLoopBody},
        {string_static("while(continue) {}"), ScriptResult_NotValidOutsideLoopBody},
        {string_static("while(break) {}"), ScriptResult_NotValidOutsideLoopBody},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptReadResult res;
      script_read(doc, binder, g_testData[i].input, &res);

      check_msg(
          res.type == g_testData[i].expected,
          "{} == {} [{}]",
          script_result_fmt(res.type),
          script_result_fmt(g_testData[i].expected),
          fmt_text(g_testData[i].input));
    }
  }

  it("can read all input") {
    ScriptReadResult res;
    script_read(doc, binder, string_lit("1  "), &res);

    check_require(res.type == ScriptResult_Success);
  }

  it("fails when recursing too deep") {
    DynString str = dynstring_create(g_alloc_scratch, 256);
    dynstring_append_chars(&str, '(', 100);

    ScriptReadResult res;
    script_read(doc, binder, dynstring_view(&str), &res);

    check_eq_int(res.type, ScriptResult_RecursionLimitExceeded);

    dynstring_destroy(&str);
  }

  it("fails when using too many variables") {
    DynString str = dynstring_create(g_alloc_scratch, 1024);
    for (u32 i = 0; i != (script_var_count + 1); ++i) {
      dynstring_append(&str, fmt_write_scratch("var v{} = 42;", fmt_int(i)));
    }

    ScriptReadResult res;
    script_read(doc, binder, dynstring_view(&str), &res);

    check_eq_int(res.type, ScriptResult_VariableLimitExceeded);

    dynstring_destroy(&str);
  }

  it("reports error source positions") {
    static const struct {
      String input;
      u16    startLine, startCol;
      u32    endLine, endCol;
    } g_testData[] = {
        {string_static("test"), .startLine = 0, .startCol = 0, .endLine = 0, .endCol = 4},
        {string_static(" \n test "), .startLine = 1, .startCol = 1, .endLine = 1, .endCol = 5},
        {string_static("// Test\n test"), .startLine = 1, .startCol = 1, .endLine = 1, .endCol = 5},
        {string_static(" 你好世界 "), .startLine = 0, .startCol = 1, .endLine = 0, .endCol = 5},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      const String     input = g_testData[i].input;
      ScriptReadResult res;
      script_read(doc, binder, input, &res);

      check_require(res.type != ScriptResult_Success);

      const ScriptPosHuman humanPosStart = script_pos_humanize(input, res.errorRange.start);
      const ScriptPosHuman humanPosEnd   = script_pos_humanize(input, res.errorRange.end);
      check_eq_int(humanPosStart.line, g_testData[i].startLine);
      check_eq_int(humanPosStart.column, g_testData[i].startCol);
      check_eq_int(humanPosEnd.line, g_testData[i].endLine);
      check_eq_int(humanPosEnd.column, g_testData[i].endCol);
    }
  }

  teardown() {
    script_destroy(doc);
    script_binder_destroy(binder);
  }
}
