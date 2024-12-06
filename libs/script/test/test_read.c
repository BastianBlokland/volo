#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_stringtable.h"
#include "script_binder.h"
#include "script_diag.h"
#include "script_read.h"

#include "utils_internal.h"

spec(read) {
  ScriptDoc*     doc             = null;
  ScriptDiagBag* diags           = null;
  ScriptBinder*  binder          = null;
  StringTable*   stringtableNull = null;
  ScriptDiagBag* diagsNull       = null;
  ScriptSymBag*  symsNull        = null;

  setup() {
    doc   = script_create(g_allocHeap);
    diags = script_diag_bag_create(g_allocHeap, ScriptDiagFilter_All);

    binder = script_binder_create(g_allocHeap, string_lit("test"), ScriptBinderFlags_None);
    const String           documentation = string_empty;
    const ScriptSig*       nullSig       = null;
    const ScriptBinderFunc nullFunc      = null;
    script_binder_declare(binder, string_lit("bind_test_1"), documentation, nullSig, nullFunc);
    script_binder_declare(binder, string_lit("bind_test_2"), documentation, nullSig, nullFunc);
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
        {string_static("\"Hello World\""), string_static("[value: Hello World]")},
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
            string_static("magnitude(1)"),
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
            string_static("vec3(1, 2, 3)"),
            string_static("[intrinsic: vec3-compose]\n"
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
            string_static("vec_x(1)"),
            string_static("[intrinsic: vec-x]\n"
                          "  [value: 1]"),
        },
        {
            string_static("vec_y(1)"),
            string_static("[intrinsic: vec-y]\n"
                          "  [value: 1]"),
        },
        {
            string_static("vec_z(1)"),
            string_static("[intrinsic: vec-z]\n"
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
        {
            string_static("return"),
            string_static("[intrinsic: return]\n"
                          "  [value: null]"),
        },
        {
            string_static("return 42"),
            string_static("[intrinsic: return]\n"
                          "  [value: 42]"),
        },
        {
            string_static("return null"),
            string_static("[intrinsic: return]\n"
                          "  [value: null]"),
        },
        {
            string_static("return; 42"),
            string_static("[block]\n"
                          "  [intrinsic: return]\n"
                          "    [value: null]\n"
                          "  [value: 42]"),
        },
        {
            string_static("{ return }"),
            string_static("[block]\n"
                          "  [intrinsic: return]\n"
                          "    [value: null]"),
        },
        {
            string_static("{ return 42 }"),
            string_static("[block]\n"
                          "  [intrinsic: return]\n"
                          "    [value: 42]"),
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
            string_static("[intrinsic: select]\n"
                          "  [value: true]\n"
                          "  [block]\n"
                          "    [value: 2]\n"
                          "  [value: null]"),
        },
        {
            string_static("if(true) {2} else {3}"),
            string_static("[intrinsic: select]\n"
                          "  [value: true]\n"
                          "  [block]\n"
                          "    [value: 2]\n"
                          "  [block]\n"
                          "    [value: 3]"),
        },
        {
            string_static("if(true) {} else {}"),
            string_static("[intrinsic: select]\n"
                          "  [value: true]\n"
                          "  [block]\n"
                          "    [value: null]\n"
                          "  [block]\n"
                          "    [value: null]"),
        },
        {
            string_static("if(false) {2} else if(true) {3}"),
            string_static("[intrinsic: select]\n"
                          "  [value: false]\n"
                          "  [block]\n"
                          "    [value: 2]\n"
                          "  [intrinsic: select]\n"
                          "    [value: true]\n"
                          "    [block]\n"
                          "      [value: 3]\n"
                          "    [value: null]"),
        },
        {
            string_static("if(false) {2} else if(true) {3} else {4}"),
            string_static("[intrinsic: select]\n"
                          "  [value: false]\n"
                          "  [block]\n"
                          "    [value: 2]\n"
                          "  [intrinsic: select]\n"
                          "    [value: true]\n"
                          "    [block]\n"
                          "      [value: 3]\n"
                          "    [block]\n"
                          "      [value: 4]"),
        },
        {
            string_static("if(var i = 42) {i} else {i}"),
            string_static("[intrinsic: select]\n"
                          "  [var-store: 0]\n"
                          "    [value: 42]\n"
                          "  [block]\n"
                          "    [var-load: 0]\n"
                          "  [block]\n"
                          "    [var-load: 0]"),
        },
        {
            string_static("if(var i = 1) {i}; if(var i = 2) {i}"),
            string_static("[block]\n"
                          "  [intrinsic: select]\n"
                          "    [var-store: 0]\n"
                          "      [value: 1]\n"
                          "    [block]\n"
                          "      [var-load: 0]\n"
                          "    [value: null]\n"
                          "  [intrinsic: select]\n"
                          "    [var-store: 0]\n"
                          "      [value: 2]\n"
                          "    [block]\n"
                          "      [var-load: 0]\n"
                          "    [value: null]"),
        },
        {
            string_static("if(true) {}; var i"),
            string_static("[block]\n"
                          "  [intrinsic: select]\n"
                          "    [value: true]\n"
                          "    [block]\n"
                          "      [value: null]\n"
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
                          "  [intrinsic: loop]\n"
                          "    [value: null]\n"
                          "    [intrinsic: less]\n"
                          "      [var-load: 0]\n"
                          "      [value: 10]\n"
                          "    [value: null]\n"
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
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [extern: 1]"),
        },
        {
            string_static("while(true) { break }"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [intrinsic: break]"),
        },
        {
            string_static("while(true) { continue }"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [intrinsic: continue]"),
        },
        {
            string_static("while(true) { while(false) {}; break }"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [intrinsic: loop]\n"
                          "      [value: null]\n"
                          "      [value: false]\n"
                          "      [value: null]\n"
                          "      [block]\n"
                          "        [value: null]\n"
                          "    [intrinsic: break]"),
        },
        {
            string_static("while(true) { var stuff = { break }}"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [block]\n"
                          "        [intrinsic: break]"),
        },

        // For expressions.
        {
            string_static("for(;;) {}"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [value: null]"),
        },
        {
            string_static("for(;;) { bind_test_1() }"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [extern: 1]"),
        },
        {
            string_static("for(var i = 0;;) { bind_test_1() }"),
            string_static("[intrinsic: loop]\n"
                          "  [var-store: 0]\n"
                          "    [value: 0]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [extern: 1]"),
        },
        {
            string_static("for(;42;) { bind_test_1() }"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: 42]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [extern: 1]"),
        },
        {
            string_static("for(;;42) { bind_test_1() }"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: 42]\n"
                          "  [block]\n"
                          "    [extern: 1]"),
        },
        {
            string_static("for(var i = 0; i != 10;) { bind_test_1() }"),
            string_static("[intrinsic: loop]\n"
                          "  [var-store: 0]\n"
                          "    [value: 0]\n"
                          "  [intrinsic: not-equal]\n"
                          "    [var-load: 0]\n"
                          "    [value: 10]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [extern: 1]"),
        },
        {
            string_static("for(var i = 0; i != 10; i += 1) { bind_test_1() }"),
            string_static("[intrinsic: loop]\n"
                          "  [var-store: 0]\n"
                          "    [value: 0]\n"
                          "  [intrinsic: not-equal]\n"
                          "    [var-load: 0]\n"
                          "    [value: 10]\n"
                          "  [var-store: 0]\n"
                          "    [intrinsic: add]\n"
                          "      [var-load: 0]\n"
                          "      [value: 1]\n"
                          "  [block]\n"
                          "    [extern: 1]"),
        },
        {
            string_static("for(;;) { break }"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [intrinsic: break]"),
        },
        {
            string_static("for(;;) { continue }"),
            string_static("[intrinsic: loop]\n"
                          "  [value: null]\n"
                          "  [value: true]\n"
                          "  [value: null]\n"
                          "  [block]\n"
                          "    [intrinsic: continue]"),
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
            string_static("[block]\n"
                          "  [value: 1]"),
        },
        {
            string_static("{1;}"),
            string_static("[block]\n"
                          "  [value: 1]"),
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
        {
            string_static("var sqrOf42 = { var i = 42; i * i }"),
            string_static("[var-store: 0]\n"
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [value: 42]\n"
                          "    [intrinsic: mul]\n"
                          "      [var-load: 0]\n"
                          "      [var-load: 0]"),
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
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [value: 1]\n"
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [value: 2]\n"
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [value: 3]\n"
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [value: 4]"),
        },
        {
            string_static("{var a = 1}; {var a = 2}; {var a = 3}; {var a = 4}"),
            string_static("[block]\n"
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [value: 1]\n"
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [value: 2]\n"
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [value: 3]\n"
                          "  [block]\n"
                          "    [var-store: 0]\n"
                          "      [value: 4]"),
        },
        {
            string_static("var a = 42; {a}"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: 42]\n"
                          "  [block]\n"
                          "    [var-load: 0]"),
        },
        {
            string_static("var a = 42; {a * a}"),
            string_static("[block]\n"
                          "  [var-store: 0]\n"
                          "    [value: 42]\n"
                          "  [block]\n"
                          "    [intrinsic: mul]\n"
                          "      [var-load: 0]\n"
                          "      [var-load: 0]"),
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
      const ScriptExpr expr =
          script_read(doc, binder, g_testData[i].input, g_stringtable, diagsNull, symsNull);

      check_require_msg(!sentinel_check(expr), "Read failed [{}]", fmt_text(g_testData[i].input));
      check_expr_str(doc, expr, g_testData[i].expect);
    }
  }

  it("fails when parsing invalid expressions") {
    static const struct {
      String         input;
      ScriptDiagKind expected;
    } g_testData[] = {
        {string_static("}"), ScriptDiag_InvalidPrimaryExpr},
        {string_static("1 }"), ScriptDiag_MissingSemicolon},
        {string_static("1 1"), ScriptDiag_MissingSemicolon},
        {string_static("hello"), ScriptDiag_NoVarFoundForId},
        {string_static("<"), ScriptDiag_InvalidPrimaryExpr},
        {string_static("1 &&"), ScriptDiag_MissingPrimaryExpr},
        {string_static("1 ||"), ScriptDiag_MissingPrimaryExpr},
        {string_static("1 <"), ScriptDiag_MissingPrimaryExpr},
        {string_static("1 < hello"), ScriptDiag_NoVarFoundForId},
        {string_static(")"), ScriptDiag_InvalidPrimaryExpr},
        {string_static("("), ScriptDiag_MissingPrimaryExpr},
        {string_static("(1"), ScriptDiag_UnclosedParenthesizedExpr},
        {string_static("(1 1"), ScriptDiag_UnclosedParenthesizedExpr},
        {string_static("!"), ScriptDiag_MissingPrimaryExpr},
        {string_static(";"), ScriptDiag_UnexpectedSemicolon},
        {string_static("1 ; ;"), ScriptDiag_UnexpectedSemicolon},
        {string_static("1;;"), ScriptDiag_UnexpectedSemicolon},
        {string_static("?"), ScriptDiag_InvalidPrimaryExpr},
        {string_static("1?"), ScriptDiag_MissingPrimaryExpr},
        {string_static("1 ?"), ScriptDiag_MissingPrimaryExpr},
        {string_static("1?1"), ScriptDiag_MissingColonInSelectExpr},
        {string_static("1 ? 1"), ScriptDiag_MissingColonInSelectExpr},
        {string_static("1 ? foo"), ScriptDiag_NoVarFoundForId},
        {string_static("1 ? 1 : foo"), ScriptDiag_NoVarFoundForId},
        {string_static("1 ? 1 : 1 2"), ScriptDiag_MissingSemicolon},
        {string_static("distance"), ScriptDiag_NoVarFoundForId},
        {string_static("distance("), ScriptDiag_UnterminatedArgumentList},
        {string_static("distance(,"), ScriptDiag_MissingPrimaryExpr},
        {string_static("distance(1 2"), ScriptDiag_UnterminatedArgumentList},
        {string_static("distance(1,"), ScriptDiag_MissingPrimaryExpr},
        {string_static("distance(1,2,3)"), ScriptDiag_IncorrectArgCountForBuiltinFunc},
        {string_static("hello()"), ScriptDiag_NoFuncFoundForId},
        {string_static("hello(null)"), ScriptDiag_NoFuncFoundForId},
        {string_static("hello(1,2,3,4,5)"), ScriptDiag_NoFuncFoundForId},
        {string_static("hello(1 + 2 + 4, 5 + 6 + 7)"), ScriptDiag_NoFuncFoundForId},
        {string_static("hello(1,2,3,4,5,6,7,8,9,10)"), ScriptDiag_NoFuncFoundForId},
        {string_static("hello(1,2,3,4,5,6,7,8,9,10,"), ScriptDiag_ArgumentCountExceedsMaximum},
        {string_static("{"), ScriptDiag_UnterminatedBlock},
        {string_static("{1"), ScriptDiag_UnterminatedBlock},
        {string_static("{1;"), ScriptDiag_UnterminatedBlock},
        {string_static("{1;2"), ScriptDiag_UnterminatedBlock},
        {string_static("{1;2;"), ScriptDiag_UnterminatedBlock},
        {string_static("if"), ScriptDiag_InvalidIf},
        {string_static("if("), ScriptDiag_UnterminatedArgumentList},
        {string_static("if()"), ScriptDiag_InvalidConditionCount},
        {string_static("if(1,2)"), ScriptDiag_InvalidConditionCount},
        {string_static("if(1)"), ScriptDiag_BlockExpected},
        {string_static("if(1) 1"), ScriptDiag_BlockExpected},
        {string_static("if(1) {1} else"), ScriptDiag_BlockOrIfExpected},
        {string_static("if(1) {1}; 2 else 3"), ScriptDiag_MissingSemicolon},
        {string_static("if(1) {var i = 42} else {i}"), ScriptDiag_NoVarFoundForId},
        {string_static("if(1) {2}; else {2}"), ScriptDiag_InvalidPrimaryExpr},
        {string_static("if(var i = 42) {}; i"), ScriptDiag_NoVarFoundForId},
        {string_static("while"), ScriptDiag_InvalidWhileLoop},
        {string_static("while("), ScriptDiag_UnterminatedArgumentList},
        {string_static("while()"), ScriptDiag_InvalidConditionCount},
        {string_static("while(1,2)"), ScriptDiag_InvalidConditionCount},
        {string_static("while(1)"), ScriptDiag_BlockExpected},
        {string_static("while(1) 1"), ScriptDiag_BlockExpected},
        {string_static("while(var i = 42) {}; i"), ScriptDiag_NoVarFoundForId},
        {string_static("for"), ScriptDiag_InvalidForLoop},
        {string_static("for("), ScriptDiag_MissingPrimaryExpr},
        {string_static("for()"), ScriptDiag_ForLoopCompMissing},
        {string_static("for(1,2)"), ScriptDiag_ForLoopSeparatorMissing},
        {string_static("for(1)"), ScriptDiag_ForLoopSeparatorMissing},
        {string_static("for(1 1) 1"), ScriptDiag_ForLoopSeparatorMissing},
        {string_static("for(1;)"), ScriptDiag_ForLoopCompMissing},
        {string_static("for(;;;)"), ScriptDiag_UnexpectedSemicolon},
        {string_static("for(;;"), ScriptDiag_MissingPrimaryExpr},
        {string_static("for(;;1"), ScriptDiag_InvalidForLoop},
        {string_static("for(var i = 0;;) 1"), ScriptDiag_BlockExpected},
        {string_static("for(var i = 0;;) {}; i"), ScriptDiag_NoVarFoundForId},
        {string_static("1 ? var i = 42 : i"), ScriptDiag_NoVarFoundForId},
        {string_static("false && var i = 42; i"), ScriptDiag_NoVarFoundForId},
        {string_static("true || var i = 42; i"), ScriptDiag_NoVarFoundForId},
        {string_static("1 ?? var i = 42; i"), ScriptDiag_NoVarFoundForId},
        {string_static("random"), ScriptDiag_NoVarFoundForId},
        {string_static("bind_test_1"), ScriptDiag_NoVarFoundForId},
        {string_static("var i; { var i = 99 }"), ScriptDiag_VarIdConflicts},
        {string_static("var"), ScriptDiag_VarIdInvalid},
        {string_static("var 2"), ScriptDiag_VarIdInvalid},
        {string_static("var pi"), ScriptDiag_VarIdConflicts},
        {string_static("var a; var a"), ScriptDiag_VarIdConflicts},
        {string_static("var a ="), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a = var b = 2"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a = while(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a = for(;;) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a = if(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a = return"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a = var b = 2"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a = while(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a = for(;;) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a = if(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a = return"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a += var b = 2"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a += while(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a += for(;;) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a += if(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a; a += return"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a = var b = 2"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a = while(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a = for(;;) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a = if(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a = return"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a += var b = 2"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a += while(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a += for(;;) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a += if(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("$a += return"), ScriptDiag_MissingPrimaryExpr},
        {string_static("return var b"), ScriptDiag_MissingPrimaryExpr},
        {string_static("return while(1) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("return for(;;) {}"), ScriptDiag_MissingPrimaryExpr},
        {string_static("return return"), ScriptDiag_MissingPrimaryExpr},
        {string_static("var a = a"), ScriptDiag_NoVarFoundForId},
        {string_static("b ="), ScriptDiag_MissingPrimaryExpr},
        {string_static("var b; b ="), ScriptDiag_MissingPrimaryExpr},
        {string_static("a"), ScriptDiag_NoVarFoundForId},
        {string_static("{var a}; a"), ScriptDiag_NoVarFoundForId},
        {string_static("a += 1"), ScriptDiag_NoVarFoundForId},
        {string_static("var a; a +="), ScriptDiag_MissingPrimaryExpr},
        {string_static("continue"), ScriptDiag_OnlyValidInLoop},
        {string_static("break"), ScriptDiag_OnlyValidInLoop},
        {string_static("while(continue) {}"), ScriptDiag_OnlyValidInLoop},
        {string_static("while(break) {}"), ScriptDiag_OnlyValidInLoop},
        {string_static("for(break;;) {}"), ScriptDiag_OnlyValidInLoop},
        {string_static("for(continue;;) {}"), ScriptDiag_OnlyValidInLoop},
        {string_static("for(;break;) {}"), ScriptDiag_OnlyValidInLoop},
        {string_static("for(;continue;) {}"), ScriptDiag_OnlyValidInLoop},
        {string_static("for(;;break) {}"), ScriptDiag_OnlyValidInLoop},
        {string_static("for(;;continue) {}"), ScriptDiag_OnlyValidInLoop},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      script_diag_clear(diags);
      script_read(doc, binder, g_testData[i].input, stringtableNull, diags, symsNull);

      const u32 errorCount = script_diag_count(diags, ScriptDiagFilter_Error);
      check_require_msg(errorCount >= 1, "errorCount >= 1 [{}]", fmt_text(g_testData[i].input));

      const ScriptDiag* diag = script_diag_first(diags, ScriptDiagFilter_Error);
      check_msg(
          diag->kind == g_testData[i].expected,
          "{} == {} [{}]",
          fmt_int(diag->kind),
          fmt_int(g_testData[i].expected),
          fmt_text(g_testData[i].input));
    }
  }

  it("can return programs with semantic errors") {
    static const struct {
      String         input;
      ScriptDiagKind expected;
    } g_testData[] = {
        {string_static("hello()"), ScriptDiag_NoFuncFoundForId},
        {string_static("type(1 +)"), ScriptDiag_MissingPrimaryExpr},
        {string_static("type(, 1)"), ScriptDiag_MissingPrimaryExpr},
        {string_static("type(,)"), ScriptDiag_MissingPrimaryExpr},
        {string_static("type(1,)"), ScriptDiag_MissingPrimaryExpr},
        {string_static("type(1 +,)"), ScriptDiag_MissingPrimaryExpr},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      // NOTE: Invalid programs are only returned when providing a diagnostic-bag as otherwise there
      // would be no way to determine if the output program was valid.
      script_diag_clear(diags);
      const ScriptExpr expr =
          script_read(doc, binder, g_testData[i].input, stringtableNull, diags, symsNull);

      check_msg(!sentinel_check(expr), "valid expression [{}]", fmt_text(g_testData[i].input));

      const u32 errorCount = script_diag_count(diags, ScriptDiagFilter_Error);
      check_require_msg(errorCount >= 1, "errorCount >= 1 [{}]", fmt_text(g_testData[i].input));

      const ScriptDiag* diag = script_diag_first(diags, ScriptDiagFilter_Error);
      check(diag->kind == g_testData[i].expected);
    }
  }

  it("can read all input") {
    const ScriptExpr expr =
        script_read(doc, binder, string_lit("1  "), stringtableNull, diagsNull, symsNull);

    check_require(!sentinel_check(expr));
  }

  it("fails when recursing too deep") {
    DynString str = dynstring_create(g_allocScratch, 256);
    dynstring_append_chars(&str, '(', 100);

    script_diag_clear(diags);
    script_read(doc, binder, dynstring_view(&str), stringtableNull, diags, symsNull);

    check_require(script_diag_count(diags, ScriptDiagFilter_Error) == 1);
    const ScriptDiag* diag = script_diag_first(diags, ScriptDiagFilter_Error);
    check_eq_int(diag->kind, ScriptDiag_RecursionLimitExceeded);

    dynstring_destroy(&str);
  }

  it("fails when using too many variables") {
    DynString str = dynstring_create(g_allocScratch, 1024);
    for (u32 i = 0; i != (script_var_count + 1); ++i) {
      dynstring_append(&str, fmt_write_scratch("var v{} = 42;", fmt_int(i)));
    }

    script_diag_clear(diags);
    script_read(doc, binder, dynstring_view(&str), stringtableNull, diags, symsNull);

    check_require(script_diag_count(diags, ScriptDiagFilter_Error) == 1);
    const ScriptDiag* diag = script_diag_first(diags, ScriptDiagFilter_Error);
    check_eq_int(diag->kind, ScriptDiag_VarLimitExceeded);

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
      const String input = g_testData[i].input;
      script_diag_clear(diags);
      script_read(doc, binder, input, stringtableNull, diags, symsNull);

      check_require(script_diag_count(diags, ScriptDiagFilter_All) == 1);

      const ScriptDiag*      diag       = script_diag_data(diags);
      const ScriptPosLineCol rangeStart = script_pos_to_line_col(input, diag->range.start);
      const ScriptPosLineCol rangeEnd   = script_pos_to_line_col(input, diag->range.end);
      check_eq_int(rangeStart.line, g_testData[i].startLine);
      check_eq_int(rangeStart.column, g_testData[i].startCol);
      check_eq_int(rangeEnd.line, g_testData[i].endLine);
      check_eq_int(rangeEnd.column, g_testData[i].endCol);
    }
  }

  teardown() {
    script_destroy(doc);
    script_diag_bag_destroy(diags);
    script_binder_destroy(binder);
  }
}
