#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_math.h"
#include "script_eval.h"
#include "script_mem.h"
#include "script_read.h"

#include "utils_internal.h"

spec(eval) {
  ScriptDoc* doc = null;
  ScriptMem* mem = null;

  setup() {
    doc = script_create(g_alloc_heap);
    mem = script_mem_create(g_alloc_heap);

    script_mem_set(mem, string_hash_lit("v1"), script_bool(true));
    script_mem_set(mem, string_hash_lit("v2"), script_number(1337));
    script_mem_set(mem, string_hash_lit("v3"), script_null());
  }

  it("can evaluate expressions") {
    const struct {
      String    input;
      ScriptVal expected;
    } testData[] = {
        // Literal values.
        {string_static("null"), script_null()},
        {string_static("42.1337"), script_number(42.1337)},
        {string_static("true"), script_bool(true)},
        {string_static("false"), script_bool(false)},
        {string_static("pi"), script_number(math_pi_f64)},
        {string_static("deg_to_rad"), script_number(math_deg_to_rad)},
        {string_static("rad_to_deg"), script_number(math_rad_to_deg)},
        {string_static("up"), script_vector3(geo_up)},
        {string_static("down"), script_vector3(geo_down)},
        {string_static("left"), script_vector3(geo_left)},
        {string_static("right"), script_vector3(geo_right)},
        {string_static("forward"), script_vector3(geo_forward)},
        {string_static("backward"), script_vector3(geo_backward)},

        // Conversions.
        {string_static("vector(1,2,3)"), script_vector3_lit(1, 2, 3)},
        {string_static("vector(1,true,3)"), script_null()},
        {string_static("vector(1 + 2, 2 + 3, 3 + 4)"), script_vector3_lit(3, 5, 7)},
        {string_static("vector_x(vector(1, 2, 3))"), script_number(1)},
        {string_static("vector_y(vector(1, 2, 3))"), script_number(2)},
        {string_static("vector_z(vector(1, 2, 3))"), script_number(3)},
        {string_static("vector_x(vector(1, true, 3))"), script_null()},
        {string_static("vector_y(vector(1, true, 3))"), script_null()},
        {string_static("vector_z(vector(1, true, 3))"), script_null()},

        // Memory loads.
        {string_static("$v1"), script_bool(true)},
        {string_static("$v2"), script_number(1337)},
        {string_static("$v3"), script_null()},
        {string_static("$non_existent"), script_null()},

        // Memory stores.
        {string_static("$v4 = true"), script_bool(true)},

        // Arithmetic.
        {string_static("-42"), script_number(-42)},
        {string_static("--42"), script_number(42)},
        {string_static("---42"), script_number(-42)},
        {string_static("-42 + -41"), script_number(-83)},
        {string_static("1 + 2"), script_number(3)},
        {string_static("1 + 2 + 3"), script_number(6)},
        {string_static("-(1 + 2 + 3)"), script_number(-6)},
        {string_static("2 * 4 + 2 / 8"), script_number(8.25)},
        {string_static("1 + null"), script_null()},
        {string_static("null + 1"), script_null()},
        {string_static("null + null"), script_null()},
        {string_static("1 - 2"), script_number(-1)},
        {string_static("1 - 2 - 3"), script_number(-4)},
        {string_static("1 + $v2"), script_number(1338)},
        {string_static("!true"), script_bool(false)},
        {string_static("!false"), script_bool(true)},
        {string_static("distance(1)"), script_number(1)},
        {string_static("distance(-1)"), script_number(1)},
        {string_static("distance(0, 0)"), script_number(0)},
        {string_static("distance(-1, 1)"), script_number(2)},
        {string_static("distance(42, 1337)"), script_number(1295)},
        {string_static("distance(vector(0,2,0))"), script_number(2)},
        {string_static("distance(vector(1,2,3), vector(1,3,3))"), script_number(1)},
        {string_static("angle(up, down)"), script_number(math_pi_f64)},
        {string_static("angle(up, up)"), script_number(0)},
        {string_static("angle(up, down) == pi"), script_bool(true)},
        {string_static("up * 42"), script_vector3_lit(0, 42, 0)},
        {string_static("up * 42 / 42"), script_vector3(geo_up)},
        {string_static("round_down(1.6)"), script_number(1.0)},
        {string_static("round_down(1.0)"), script_number(1.0)},
        {string_static("round_up(1.0)"), script_number(1.0)},
        {string_static("round_up(1.1)"), script_number(2.0)},
        {string_static("round_nearest(1.1)"), script_number(1.0)},
        {string_static("round_nearest(1.5)"), script_number(2.0)},

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
        {string_static("false && ($a = 1; false); $a"), script_null()},
        {string_static("true && ($b = 2; false); $b"), script_number(2)},
        {string_static("false || ($c = 3; false); $c"), script_number(3)},
        {string_static("true || ($d = 4; false); $d"), script_null()},

        // Condition expressions.
        {string_static("null ?? null"), script_null()},
        {string_static("null ?? true"), script_bool(true)},
        {string_static("false ?? true"), script_bool(false)},
        {string_static("null ?? ($i = 10; false); $i"), script_number(10)},
        {string_static("1 ?? ($j = 11; false); $j"), script_null()},
        {string_static("true ? 42 : 1337"), script_number(42)},
        {string_static("false ? 42 : 1337"), script_number(1337)},
        {string_static("2 > 1 ? 42 : 1337"), script_number(42)},
        {string_static("(true ? $k = 22 : 0); $k"), script_number(22)},
        {string_static("(true ? 0 : $l = 33); $l"), script_null()},
        {string_static("(false ? $m = 44 : 0); $m"), script_null()},
        {string_static("(false ? 0 : $n = 55); $n"), script_number(55)},

        // Blocks.
        {string_static("1; 2; 3"), script_number(3)},
        {string_static("1; 2; 3;"), script_number(3)},
        {string_static("$e = 1; $e + 41"), script_number(42)},
        {string_static("$f = 1; $g = 5; $h = 42; $f + $g + $h"), script_number(48)},

        // Compound expressions.
        {string_static("1 + 2 == 4 - 1"), script_bool(true)},
        {string_static("1 + (2 == 4) - 1"), script_null()},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      ScriptReadResult readRes;
      script_read(doc, testData[i].input, &readRes);
      check_require_msg(
          readRes.type == ScriptResult_Success, "Read failed ({})", fmt_text(testData[i].input));

      const ScriptVal evalRes = script_eval(doc, mem, readRes.expr);
      check_msg(
          script_val_equal(evalRes, testData[i].expected),
          "{} == {} ({})",
          script_val_fmt(evalRes),
          script_val_fmt(testData[i].expected),
          fmt_text(testData[i].input));
    }
  }

  it("can store memory values") {
    ScriptReadResult readRes;
    script_read(doc, string_lit("$test1 = 42; $test2 = 1337; $test3 = false"), &readRes);
    check_require(readRes.type == ScriptResult_Success);

    script_eval(doc, mem, readRes.expr);
    check_eq_val(script_mem_get(mem, string_hash_lit("test1")), script_number(42));
    check_eq_val(script_mem_get(mem, string_hash_lit("test2")), script_number(1337));
    check_eq_val(script_mem_get(mem, string_hash_lit("test3")), script_bool(false));
  }

  teardown() {
    script_destroy(doc);
    script_mem_destroy(mem);
  }
}
