#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
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

        // Memory loads.
        {string_static("$v1"), script_bool(true)},
        {string_static("$v2"), script_number(1337)},
        {string_static("$v3"), script_null()},
        {string_static("$non-existent"), script_null()},

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
        {string_static("1 + null"), script_null()},
        {string_static("null + 1"), script_null()},
        {string_static("null + null"), script_null()},
        {string_static("1 - 2"), script_number(-1)},
        {string_static("1 - 2 - 3"), script_number(-4)},
        {string_static("1 + $v2"), script_number(1338)},

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

        // Compound expressions.
        {string_static("1 + 2 == 4 - 1"), script_bool(true)},
        {string_static("1 + (2 == 4) - 1"), script_null()},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      ScriptReadResult readRes;
      script_read_all(doc, testData[i].input, &readRes);
      check_require(readRes.type == ScriptResult_Success);

      const ScriptVal evalRes = script_eval(doc, mem, readRes.expr);
      check_eq_val(evalRes, testData[i].expected);
    }
  }

  it("can store memory values") {
    ScriptReadResult readRes;
    script_read_all(doc, string_lit("$test = 42"), &readRes);
    check_require(readRes.type == ScriptResult_Success);

    script_eval(doc, mem, readRes.expr);
    check_eq_val(script_mem_get(mem, string_hash_lit("test")), script_number(42));
  }

  teardown() {
    script_destroy(doc);
    script_mem_destroy(mem);
  }
}
