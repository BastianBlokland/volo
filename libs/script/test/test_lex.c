#include "check_spec.h"
#include "core_array.h"
#include "script_lex.h"

#include "utils_internal.h"

spec(lex) {
  it("can equate tokens") {
    const struct {
      ScriptToken a;
      ScriptToken b;
      bool        expected;
    } testData[] = {
        {.a = tok_simple(EqEq), .b = tok_simple(EqEq), .expected = true},
        {.a = tok_simple(EqEq), .b = tok_simple(BangEq), .expected = false},

        {.a = tok_number(42), .b = tok_number(42), .expected = true},
        {.a = tok_number(42), .b = tok_number(41), .expected = false},

        {.a = tok_id_lit("HelloWorld"), .b = tok_id_lit("HelloWorld"), .expected = true},
        {.a = tok_id_lit("Hello"), .b = tok_id_lit("HelloWorld"), .expected = false},

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
        {string_static("("), tok_simple(ParenOpen)},
        {string_static(")"), tok_simple(ParenClose)},

        {string_static("="), tok_simple(Eq)},
        {string_static("=="), tok_simple(EqEq)},

        {string_static("!="), tok_simple(BangEq)},
        {string_static("!"), tok_simple(Bang)},

        {string_static("<"), tok_simple(Le)},
        {string_static("<="), tok_simple(LeEq)},

        {string_static(">"), tok_simple(Gt)},
        {string_static(">="), tok_simple(GtEq)},

        {string_static("+"), tok_simple(Plus)},
        {string_static("-"), tok_simple(Minus)},
        {string_static("*"), tok_simple(Star)},
        {string_static("/"), tok_simple(Slash)},

        {string_static("&&"), tok_simple(AmpAmp)},
        {string_static("||"), tok_simple(PipePipe)},
        {string_static("??"), tok_simple(QMarkQMark)},

        {string_static(";"), tok_simple(SemiColon)},

        {string_static("42"), tok_number(42)},
        {string_static("0.0"), tok_number(0.0)},
        {string_static("42.1337"), tok_number(42.1337)},
        {string_static(".0"), tok_number(0.0)},
        {string_static(".1"), tok_number(.1)},
        {string_static(".000000000000001337"), tok_number(.000000000000001337)},
        {string_static("0.0"), tok_number(0.0)},
        {string_static("1E+17"), tok_number(1e+17)},
        {string_static("0.17976931348623157"), tok_number(0.17976931348623157)},

        {string_static("null"), tok_id_lit("null")},
        {string_static("true"), tok_id_lit("true")},
        {string_static("hello"), tok_id_lit("hello")},
        {string_static("hello_world"), tok_id_lit("hello_world")},
        {string_static("你好世界"), tok_id_lit("你好世界")},

        {string_static("$hello"), tok_key_lit("hello")},
        {string_static("$héllo"), tok_key_lit("héllo")},
        {string_static("$hello123"), tok_key_lit("hello123")},
        {string_static("$123"), tok_key_lit("123")},
        {string_static("$123hello"), tok_key_lit("123hello")},
        {string_static("$你好世界"), tok_key_lit("你好世界")},
        {string_static(" \t $héllo"), tok_key_lit("héllo")},
        {string_static("$"), tok_err(KeyEmpty)},

        {string_static("&"), tok_err(InvalidChar)},
        {string_static("|"), tok_err(InvalidChar)},
        {string_static("?"), tok_err(InvalidChar)},
        {string_static("@"), tok_err(InvalidChar)},

        {string_static(""), tok_end()},
        {string_static(" "), tok_end()},
        {string_static("\t"), tok_end()},
        {string_static("\n"), tok_end()},
        {string_static("\r"), tok_end()},
        {string_static("\0"), tok_end()},
        {string_static(" \t\n\r"), tok_end()},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      ScriptToken  token;
      const String rem = script_lex(testData[i].input, null, &token);

      check_msg(string_is_empty(rem), "Unexpected remaining input: '{}'", fmt_text(rem));
      check_eq_tok(&token, &testData[i].expected);
    }
  }
}
