#include "check_spec.h"
#include "core_array.h"
#include "script_lex.h"

#include "utils_internal.h"

spec(lex) {
  it("can equate token") {
    const struct {
      ScriptToken a;
      ScriptToken b;
      bool        expected;
    } testData[] = {
        {.a = tok_simple(OpEqEq), .b = tok_simple(OpEqEq), .expected = true},
        {.a = tok_simple(OpEqEq), .b = tok_simple(OpBangEq), .expected = false},
        {.a = tok_number(42), .b = tok_number(42), .expected = true},
        {.a = tok_number(42), .b = tok_number(41), .expected = false},
        {.a = tok_bool(true), .b = tok_bool(true), .expected = true},
        {.a = tok_bool(true), .b = tok_bool(false), .expected = false},
        {.a = tok_key_lit("HelloWorld"), .b = tok_key_lit("HelloWorld"), .expected = true},
        {.a = tok_key_lit("Hello"), .b = tok_key_lit("HelloWorld"), .expected = false},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_eq_tok(&testData[i].a, &testData[i].b);
      } else {
        check_neq_tok(&testData[i].a, &testData[i].b);
      }
    }
  }

  it("can identify tokens") {
    const struct {
      String      input;
      ScriptToken expected;
    } testData[] = {
        {string_static("=="), tok_simple(OpEqEq)},
        {string_static("!="), tok_simple(OpBangEq)},
        {string_static("<"), tok_simple(OpLe)},
        {string_static("<="), tok_simple(OpLeEq)},
        {string_static(">"), tok_simple(OpGt)},
        {string_static(">="), tok_simple(OpGtEq)},
        {string_static("null"), tok_null()},
        {string_static("42"), tok_number(42)},
        {string_static("true"), tok_bool(true)},
        {string_static("false"), tok_bool(false)},
        {string_static("$hello"), tok_key_lit("hello")},
        {string_static("|"), tok_err(ScriptError_InvalidChar)},
        {string_static(""), tok_end()},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      ScriptToken  token;
      const String rem = script_lex(testData[i].input, null, &token);

      check_msg(string_is_empty(rem), "Unexpected remaining input: '{}'", fmt_text(rem));
      check_eq_tok(&token, &testData[i].expected);
    }
  }
}
