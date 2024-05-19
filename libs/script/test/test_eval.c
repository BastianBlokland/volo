#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_math.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "script_binder.h"
#include "script_eval.h"
#include "script_mem.h"
#include "script_read.h"

#include "utils_internal.h"

typedef struct {
  u32 counter;
} ScriptEvalTestCtx;

static ScriptVal test_increase_counter(void* ctx, const ScriptArgs args, ScriptError* err) {
  (void)args;
  (void)err;

  ScriptEvalTestCtx* typedCtx = ctx;
  ++typedCtx->counter;
  return script_null();
}

static ScriptVal test_return_null(void* ctx, const ScriptArgs args, ScriptError* err) {
  (void)ctx;
  (void)args;
  (void)err;

  return script_null();
}

static ScriptVal test_return_first(void* ctx, const ScriptArgs args, ScriptError* err) {
  (void)ctx;
  (void)err;

  return args.count ? args.values[0] : script_null();
}

spec(eval) {
  ScriptMem      mem;
  ScriptDoc*     doc         = null;
  ScriptBinder*  binder      = null;
  void*          bindCtxNull = null;
  ScriptDiagBag* diagsNull   = null;
  ScriptSymBag*  symsNull    = null;

  setup() {
    mem = script_mem_create();
    doc = script_create(g_allocHeap);

    script_mem_store(&mem, string_hash_lit("v1"), script_bool(true));
    script_mem_store(&mem, string_hash_lit("v2"), script_num(1337));
    script_mem_store(&mem, string_hash_lit("v3"), script_null());

    binder                         = script_binder_create(g_allocHeap);
    const String     documentation = string_empty;
    const ScriptSig* nullSig       = null;
    script_binder_declare(
        binder, string_lit("test_return_null"), documentation, nullSig, test_return_null);
    script_binder_declare(
        binder, string_lit("test_return_first"), documentation, nullSig, test_return_first);
    script_binder_declare(
        binder, string_lit("test_increase_counter"), documentation, nullSig, test_increase_counter);
    script_binder_finalize(binder);
  }

  it("can evaluate expressions") {
    const struct {
      String    input;
      ScriptVal expected;
    } testData[] = {
        // Literal values.
        {string_static(""), script_null()},
        {string_static("null"), script_null()},
        {string_static("42.1337"), script_num(42.1337)},
        {string_static("true"), script_bool(true)},
        {string_static("false"), script_bool(false)},
        {string_static("pi"), script_num(math_pi_f64)},
        {string_static("deg_to_rad"), script_num(math_deg_to_rad)},
        {string_static("rad_to_deg"), script_num(math_rad_to_deg)},
        {string_static("up"), script_vec3(geo_up)},
        {string_static("down"), script_vec3(geo_down)},
        {string_static("left"), script_vec3(geo_left)},
        {string_static("right"), script_vec3(geo_right)},
        {string_static("forward"), script_vec3(geo_forward)},
        {string_static("backward"), script_vec3(geo_backward)},
        {string_static("red"), script_color(geo_color_red)},

        // Type check.
        {string_static("type(null)"), script_str(string_hash_lit("null"))},
        {string_static("type(1)"), script_str(string_hash_lit("num"))},
        {string_static("type(true)"), script_str(string_hash_lit("bool"))},
        {string_static("type(vec3(1,2,3))"), script_str(string_hash_lit("vec3"))},
        {string_static("type(\"Hello\")"), script_str(string_hash_lit("str"))},

        // Conversions.
        {string_static("vec3(1,2,3)"), script_vec3_lit(1, 2, 3)},
        {string_static("vec3(1,true,3)"), script_null()},
        {string_static("vec3(1 + 2, 2 + 3, 3 + 4)"), script_vec3_lit(3, 5, 7)},
        {string_static("vec_x(vec3(1, 2, 3))"), script_num(1)},
        {string_static("vec_y(vec3(1, 2, 3))"), script_num(2)},
        {string_static("vec_z(vec3(1, 2, 3))"), script_num(3)},
        {string_static("vec_x(vec3(1, true, 3))"), script_null()},
        {string_static("vec_y(vec3(1, true, 3))"), script_null()},
        {string_static("vec_z(vec3(1, true, 3))"), script_null()},

        // Variable access.
        {string_static("var i"), script_null()},
        {string_static("var i = 42"), script_num(42)},
        {string_static("var i; i"), script_null()},
        {string_static("var i = 42; i"), script_num(42)},
        {string_static("{var i = 42}; var i = 1; i"), script_num(1)},

        // Memory access.
        {string_static("$v1"), script_bool(true)},
        {string_static("$v2"), script_num(1337)},
        {string_static("$v3"), script_null()},
        {string_static("$non_existent"), script_null()},
        {string_static("$v4 = true"), script_bool(true)},
        {string_static("mem_load(\"v1\")"), script_bool(true)},
        {string_static("mem_load(\"v2\")"), script_num(1337)},
        {string_static("mem_load(\"v3\")"), script_null()},
        {string_static("mem_load(\"non_existent\")"), script_null()},
        {string_static("mem_store(\"v4\", true)"), script_bool(true)},

        // Arithmetic.
        {string_static("-42"), script_num(-42)},
        {string_static("--42"), script_num(42)},
        {string_static("---42"), script_num(-42)},
        {string_static("-42 + -41"), script_num(-83)},
        {string_static("1 + 2"), script_num(3)},
        {string_static("1 + 2 + 3"), script_num(6)},
        {string_static("-(1 + 2 + 3)"), script_num(-6)},
        {string_static("2 * 4 + 2 / 8"), script_num(8.25)},
        {string_static("1 + null"), script_null()},
        {string_static("null + 1"), script_null()},
        {string_static("null + null"), script_null()},
        {string_static("1 - 2"), script_num(-1)},
        {string_static("1 - 2 - 3"), script_num(-4)},
        {string_static("1 + $v2"), script_num(1338)},
        {string_static("!true"), script_bool(false)},
        {string_static("!false"), script_bool(true)},
        {string_static("magnitude(1)"), script_num(1)},
        {string_static("magnitude(-1)"), script_num(1)},
        {string_static("distance(0, 0)"), script_num(0)},
        {string_static("distance(-1, 1)"), script_num(2)},
        {string_static("distance(42, 1337)"), script_num(1295)},
        {string_static("magnitude(vec3(0,2,0))"), script_num(2)},
        {string_static("distance(vec3(1,2,3), vec3(1,3,3))"), script_num(1)},
        {string_static("angle(up, down)"), script_num(math_pi_f64)},
        {string_static("angle(up, up)"), script_num(0)},
        {string_static("angle(up, down) == pi"), script_bool(true)},
        {string_static("up * 42"), script_vec3_lit(0, 42, 0)},
        {string_static("up * 42 / 42"), script_vec3(geo_up)},
        {string_static("euler(0,0,0)"), script_quat(geo_quat_ident)},
        {string_static("round_down(1.6)"), script_num(1.0)},
        {string_static("round_down(1.0)"), script_num(1.0)},
        {string_static("round_up(1.0)"), script_num(1.0)},
        {string_static("round_up(1.1)"), script_num(2.0)},
        {string_static("round_nearest(1.1)"), script_num(1.0)},
        {string_static("round_nearest(1.5)"), script_num(2.0)},
        {string_static("clamp(1.5, -1, 1.25)"), script_num(1.25)},

        // Equality.
        {string_static("1 == 1"), script_bool(true)},
        {string_static("true == false"), script_bool(false)},
        {string_static("1 != 2"), script_bool(true)},
        {string_static("true != true"), script_bool(false)},

        // Comparisons.
        {string_static("2 > 1"), script_bool(true)},
        {string_static("2 < 1"), script_bool(false)},
        {string_static("2 >= 2"), script_bool(true)},
        {string_static("2 <= 2"), script_bool(true)},

        // Logic.
        {string_static("false && false"), script_bool(false)},
        {string_static("false && true"), script_bool(false)},
        {string_static("true && false"), script_bool(false)},
        {string_static("true && true"), script_bool(true)},
        {string_static("false || false"), script_bool(false)},
        {string_static("false || true"), script_bool(true)},
        {string_static("true || false"), script_bool(true)},
        {string_static("true || true"), script_bool(true)},
        {string_static("false && {$a = 1; false}; $a"), script_null()},
        {string_static("true && {$b = 2; false}; $b"), script_num(2)},
        {string_static("false || {$c = 3; false}; $c"), script_num(3)},
        {string_static("true || {$d = 4; false}; $d"), script_null()},

        // Condition expressions.
        {string_static("null ?? null"), script_null()},
        {string_static("null ?? true"), script_bool(true)},
        {string_static("false ?? true"), script_bool(false)},
        {string_static("null ?? {$i = 10; false}; $i"), script_num(10)},
        {string_static("1 ?? {$j = 11; false}; $j"), script_null()},
        {string_static("true ? 42 : 1337"), script_num(42)},
        {string_static("false ? 42 : 1337"), script_num(1337)},
        {string_static("2 > 1 ? 42 : 1337"), script_num(42)},
        {string_static("(true ? $k = 22 : 0); $k"), script_num(22)},
        {string_static("(true ? 0 : $l = 33); $l"), script_null()},
        {string_static("(false ? $m = 44 : 0); $m"), script_null()},
        {string_static("(false ? 0 : $n = 55); $n"), script_num(55)},

        // Blocks.
        {string_static("1; 2; 3"), script_num(3)},
        {string_static("1; 2; 3;"), script_num(3)},
        {string_static("$e = 1; $e + 41"), script_num(42)},
        {string_static("$f = 1; $g = 5; $h = 42; $f + $g + $h"), script_num(48)},

        // Compound expressions.
        {string_static("1 + 2 == 4 - 1"), script_bool(true)},
        {string_static("1 + (2 == 4) - 1"), script_null()},

        // External functions.
        {string_static("test_return_null()"), script_null()},
        {string_static("test_return_first(42)"), script_num(42)},
        {string_static("test_return_first(1,2,3)"), script_num(1)},

        // Loops.
        {
            string_static("var i = 0;"
                          "while(i < 10) {"
                          "  i += 1"
                          "}"),
            script_num(10),
        },
        {string_static("while(false) {}"), script_null()},
        {
            string_static("var i = 0;"
                          "while(true) {"
                          "  if((i += 1) == 10) {"
                          "    break"
                          "  }"
                          "}; i"),
            script_num(10),
        },
        {
            string_static("var i = 0;"
                          "var j = 0;"
                          "while((i += 1) < 10) {"
                          "  if(i % 2 == 0) {"
                          "    continue"
                          "  };"
                          "  j += 1"
                          "}; j"),
            script_num(5),
        },
        {
            string_static("for(var i = 0; i != 10; i += 1) {}"),
            script_null(),
        },
        {
            string_static("var i = 0; for(; i != 10; i += 1) {}; i"),
            script_num(10),
        },
        {string_static("for(;false;) {}"), script_null()},
        {
            string_static("var i = 0;"
                          "for(;; i += 1) {"
                          "  if(i == 10) {"
                          "    break"
                          "  }"
                          "}; i"),
            script_num(10),
        },
        {
            string_static("var j = 0;"
                          "for(var i = 0; i != 10; i += 1) {"
                          "  if(i % 2 == 0) {"
                          "    continue"
                          "  };"
                          "  j += 1"
                          "}; j"),
            script_num(5),
        },

        // Other.
        {string_static("assert(1)"), script_null()},
        {string_static("return"), script_null()},
        {string_static("return 42"), script_num(42)},
        {string_static("return 42 + 1337"), script_num(42 + 1337)},
        {string_static("return 42; 1337"), script_num(42)},
        {string_static("for(var i = 0;; i += 1) { if(i > 10) { return i } }"), script_num(11)},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const ScriptExpr expr = script_read(doc, binder, testData[i].input, diagsNull, symsNull);
      check_require_msg(!sentinel_check(expr), "Read failed ({})", fmt_text(testData[i].input));

      const ScriptEvalResult evalRes = script_eval(doc, &mem, expr, binder, bindCtxNull);
      check(!script_panic_valid(&evalRes.panic));
      check_msg(
          script_val_equal(evalRes.val, testData[i].expected),
          "{} == {} ({})",
          script_val_fmt(evalRes.val),
          script_val_fmt(testData[i].expected),
          fmt_text(testData[i].input));
    }
  }

  it("can store memory values") {
    const ScriptExpr expr = script_read(
        doc, binder, string_lit("$test1 = 42; $test2 = 1337; $test3 = false"), diagsNull, symsNull);
    check_require(!sentinel_check(expr));

    const ScriptEvalResult evalRes = script_eval(doc, &mem, expr, binder, bindCtxNull);
    check(!script_panic_valid(&evalRes.panic));
    check_eq_val(script_mem_load(&mem, string_hash_lit("test1")), script_num(42));
    check_eq_val(script_mem_load(&mem, string_hash_lit("test2")), script_num(1337));
    check_eq_val(script_mem_load(&mem, string_hash_lit("test3")), script_bool(false));
  }

  it("can modify the context") {
    ScriptEvalTestCtx ctx = {0};

    const ScriptExpr expr = script_read(
        doc,
        binder,
        string_lit("test_increase_counter(); test_increase_counter(); test_increase_counter()"),
        diagsNull,
        symsNull);
    check_require(!sentinel_check(expr));

    const ScriptEvalResult evalRes = script_eval(doc, &mem, expr, binder, &ctx);
    check(!script_panic_valid(&evalRes.panic));
    check_eq_int(ctx.counter, 3);
  }

  it("stops execution after a runtime-error") {
    ScriptEvalTestCtx ctx = {0};

    const ScriptExpr expr = script_read(
        doc,
        binder,
        string_lit("test_increase_counter(); assert(0); test_increase_counter()"),
        diagsNull,
        symsNull);
    check_require(!sentinel_check(expr));

    const ScriptEvalResult evalRes = script_eval(doc, &mem, expr, binder, &ctx);
    check(evalRes.panic.kind == ScriptPanic_AssertionFailed);
    check_eq_int(ctx.counter, 1);
    check_eq_val(evalRes.val, script_null());
  }

  it("limits the executed expressions count") {
    const ScriptExpr expr =
        script_read(doc, binder, string_lit("while(true) {}"), diagsNull, symsNull);
    check_require(!sentinel_check(expr));

    const ScriptEvalResult evalRes = script_eval(doc, &mem, expr, binder, bindCtxNull);
    check(evalRes.panic.kind == ScriptPanic_ExecutionLimitExceeded);
    check_eq_val(evalRes.val, script_null());
  }

  teardown() {
    script_destroy(doc);
    script_binder_destroy(binder);
    script_mem_destroy(&mem);
  }
}
