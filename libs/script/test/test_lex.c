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

        {.a = tok_string_lit("HelloWorld"), .b = tok_string_lit("HelloWorld"), .expected = true},
        {.a = tok_string_lit("Hello"), .b = tok_string_lit("HelloWorld"), .expected = false},
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
        {string_static("{"), tok_simple(CurlyOpen)},
        {string_static("}"), tok_simple(CurlyClose)},
        {string_static(","), tok_simple(Comma)},

        {string_static("="), tok_simple(Eq)},
        {string_static("=="), tok_simple(EqEq)},

        {string_static("!="), tok_simple(BangEq)},
        {string_static("!"), tok_simple(Bang)},

        {string_static("<"), tok_simple(Le)},
        {string_static("<="), tok_simple(LeEq)},

        {string_static(">"), tok_simple(Gt)},
        {string_static(">="), tok_simple(GtEq)},

        {string_static("+"), tok_simple(Plus)},
        {string_static("+="), tok_simple(PlusEq)},
        {string_static("-"), tok_simple(Minus)},
        {string_static("-="), tok_simple(MinusEq)},
        {string_static("*"), tok_simple(Star)},
        {string_static("*="), tok_simple(StarEq)},
        {string_static("/"), tok_simple(Slash)},
        {string_static("/="), tok_simple(SlashEq)},
        {string_static("%"), tok_simple(Percent)},
        {string_static("%="), tok_simple(PercentEq)},

        {string_static("&&"), tok_simple(AmpAmp)},
        {string_static("||"), tok_simple(PipePipe)},
        {string_static("?"), tok_simple(QMark)},
        {string_static("??"), tok_simple(QMarkQMark)},
        {string_static("?\?="), tok_simple(QMarkQMarkEq)},

        {string_static(":"), tok_simple(Colon)},
        {string_static(";"), tok_simple(Semicolon)},

        {string_static("42"), tok_number(42)},
        {string_static("0.0"), tok_number(0.0)},
        {string_static("42.1337"), tok_number(42.1337)},
        {string_static(".0"), tok_number(0.0)},
        {string_static(".1"), tok_number(.1)},
        {string_static(".000000000000001337"), tok_number(.000000000000001337)},
        {string_static("0.0"), tok_number(0.0)},
        {string_static("0."), tok_diag(NumberEndsWithDecPoint)},
        {string_static("0.0."), tok_diag(NumberEndsWithDecPoint)},
        {string_static("0.17976931348623157"), tok_number(0.17976931348623157)},
        {string_static("0a"), tok_diag(InvalidCharInNumber)},
        {string_static("0a123"), tok_diag(InvalidCharInNumber)},
        {string_static("0123a"), tok_diag(InvalidCharInNumber)},
        {string_static("01a2a3a"), tok_diag(InvalidCharInNumber)},
        {string_static("_42"), tok_diag(InvalidChar)},
        {string_static("42_"), tok_diag(NumberEndsWithSeparator)},
        {string_static("4_2"), tok_number(42.0)},
        {string_static("1_3_3_7"), tok_number(1337.0)},
        {string_static("13_37"), tok_number(1337.0)},
        {string_static("1_3___3_7"), tok_number(1337.0)},

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
        {string_static("$"), tok_diag(KeyEmpty)},

        {string_static("\"\""), tok_string_lit("")},
        {string_static("\"hello\""), tok_string_lit("hello")},
        {string_static("\"héllo\""), tok_string_lit("héllo")},
        {string_static("\"hello123\""), tok_string_lit("hello123")},
        {string_static("\"123\""), tok_string_lit("123")},
        {string_static("\"123 hello \""), tok_string_lit("123 hello ")},
        {string_static("\"你好\t世界\""), tok_string_lit("你好\t世界")},
        {string_static(" \t \"héllo\""), tok_string_lit("héllo")},
        {string_static("\""), tok_diag(UnterminatedString)},

        {string_static("if"), tok_simple(If)},
        {string_static("else"), tok_simple(Else)},
        {string_static("var"), tok_simple(Var)},
        {string_static("while"), tok_simple(While)},
        {string_static("for"), tok_simple(For)},
        {string_static("continue"), tok_simple(Continue)},
        {string_static("break"), tok_simple(Break)},
        {string_static("return"), tok_simple(Return)},

        {string_static("&"), tok_diag(InvalidChar)},
        {string_static("|"), tok_diag(InvalidChar)},
        {string_static("@"), tok_diag(InvalidChar)},
        {string_static("\0"), tok_diag(InvalidChar)},
        {string_static("\a"), tok_diag(InvalidChar)},

        {string_static(""), tok_end()},
        {string_static(" "), tok_end()},
        {string_static("\t"), tok_end()},
        {string_static("\n"), tok_end()},
        {string_static("\r"), tok_end()},
        {string_static(" \t\n\r"), tok_end()},
        {string_static("// Hello World"), tok_end()},
        {string_static("// Hello World +1\"!@%&*\"#%^*"), tok_end()},
        {string_static("  // Hello World \t"), tok_end()},
        {string_static("// Hello World\n42"), tok_number(42)},
        {string_static("// Hello World\r\n42"), tok_number(42)},
        {string_static("/* Hello World */"), tok_end()},
        {string_static("/* Hello World +1*\n\"!@%&\n*\"#%^*/"), tok_end()},
        {string_static("  /* Hello World */\t"), tok_end()},
        {string_static("/* Hello World"), tok_end()},
        {string_static("/* Hello World*"), tok_end()},
        {string_static("/* Hello World\r\n*/42"), tok_number(42)},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      ScriptToken  token;
      const String rem = script_lex(testData[i].input, null, &token, ScriptLexFlags_None);

      check_msg(string_is_empty(rem), "Unexpected remaining input: '{}'", fmt_text(rem));
      check_msg(
          script_token_equal(&token, &testData[i].expected),
          "{} == {} (input: '{}')",
          script_token_fmt(&token),
          script_token_fmt(&testData[i].expected),
          fmt_text(testData[i].input));
    }
  }

  it("can optionally include comment tokens") {
    ScriptToken token;
    String      str = string_lit("42 // Hello \n/* World */ 42 /* More */");

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeComments);
    check_eq_int(token.kind, ScriptTokenKind_Number);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeComments);
    check_eq_int(token.kind, ScriptTokenKind_CommentLine);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeComments);
    check_eq_int(token.kind, ScriptTokenKind_CommentBlock);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeComments);
    check_eq_int(token.kind, ScriptTokenKind_Number);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeComments);
    check_eq_int(token.kind, ScriptTokenKind_CommentBlock);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeComments);
    check_eq_int(token.kind, ScriptTokenKind_End);
  }

  it("can optionally include newline tokens") {
    ScriptToken token;
    String      str = string_lit("42 \n/* World */ 1337 \r\n\n");

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeNewlines);
    check_eq_int(token.kind, ScriptTokenKind_Number);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeNewlines);
    check_eq_int(token.kind, ScriptTokenKind_Newline);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeNewlines);
    check_eq_int(token.kind, ScriptTokenKind_Number);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeNewlines);
    check_eq_int(token.kind, ScriptTokenKind_Newline);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeNewlines);
    check_eq_int(token.kind, ScriptTokenKind_Newline);

    str = script_lex(str, null, &token, ScriptLexFlags_IncludeNewlines);
    check_eq_int(token.kind, ScriptTokenKind_End);
  }

  it("can optionally fail on whitespace") {
    ScriptToken token;

    script_lex(string_lit(" hello"), null, &token, ScriptLexFlags_NoWhitespace);
    check_eq_int(token.kind, ScriptTokenKind_Diag);

    script_lex(string_lit("hello"), null, &token, ScriptLexFlags_NoWhitespace);
    check_eq_int(token.kind, ScriptTokenKind_Identifier);
  }

  it("can trim until the next token") {
    const struct {
      String input, expected;
    } testData[] = {
        {string_static(""), string_static("")},
        {string_static("   "), string_static("")},
        {string_static("+"), string_static("+")},
        {string_static(" +"), string_static("+")},
        {string_static("    +"), string_static("+")},
        {string_static("  \t \t \r\n  \n +"), string_static("+")},
        {string_static("  \t \t \r\n  \n +   "), string_static("+   ")},
        {string_static("/ Hello World"), string_static("/ Hello World")},
        {string_static("// Hello World"), string_static("")},
        {string_static("/* Hello World"), string_static("")},
        {string_static("/* Hello World */"), string_static("")},
        {string_static("/* Hello World */ +"), string_static("+")},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const String rem = script_lex_trim(testData[i].input, ScriptLexFlags_None);
      check_eq_string(rem, testData[i].expected);
    }
  }

  it("can trim until the next token including newlines") {
    const struct {
      String input, expected;
    } testData[] = {
        {string_static(""), string_static("")},
        {string_static("   "), string_static("")},
        {string_static("+"), string_static("+")},
        {string_static(" +"), string_static("+")},
        {string_static("    +"), string_static("+")},
        {string_static("\n"), string_static("\n")},
        {string_static(" \n"), string_static("\n")},
        {string_static("  \t \t \r\n"), string_static("\n")},
        {string_static("/ Hello World"), string_static("/ Hello World")},
        {string_static("// Hello World"), string_static("")},
        {string_static("/* Hello World"), string_static("")},
        {string_static("/* Hello World */"), string_static("")},
        {string_static("/* Hello World */ \n"), string_static("\n")},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const String rem = script_lex_trim(testData[i].input, ScriptLexFlags_IncludeNewlines);
      check_eq_string(rem, testData[i].expected);
    }
  }

  it("can trim until the next token including comments") {
    const struct {
      String input, expected;
    } testData[] = {
        {string_static(""), string_static("")},
        {string_static("   "), string_static("")},
        {string_static("+"), string_static("+")},
        {string_static(" +"), string_static("+")},
        {string_static("    +"), string_static("+")},
        {string_static("  \t \t \r\n  \n +"), string_static("+")},
        {string_static("  \t \t \r\n  \n // Hello World"), string_static("// Hello World")},
        {string_static("/ Hello World"), string_static("/ Hello World")},
        {string_static("// Hello World"), string_static("// Hello World")},
        {string_static("  \t \t \r\n  \n// Hello World"), string_static("// Hello World")},
        {string_static("/* Hello World"), string_static("/* Hello World")},
        {string_static("  \t \t \r\n  \n/* Hello World"), string_static("/* Hello World")},
        {string_static("/* Hello World */"), string_static("/* Hello World */")},
        {string_static("  \t \t \r\n  \n/* Hello World */"), string_static("/* Hello World */")},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      const String rem = script_lex_trim(testData[i].input, ScriptLexFlags_IncludeComments);
      check_eq_string(rem, testData[i].expected);
    }
  }
}
