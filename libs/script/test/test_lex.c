#include "check_spec.h"
#include "core_array.h"
#include "script_error.h"
#include "script_lex.h"

spec(lex) {
  it("can equate token") {
    const struct {
      ScriptToken a;
      ScriptToken b;
      bool        expected;
    } testData[] = {
        {
            .a        = {.type = ScriptTokenType_OpEqEq},
            .b        = {.type = ScriptTokenType_OpEqEq},
            .expected = true,
        },
        {
            .a        = {.type = ScriptTokenType_OpEqEq},
            .b        = {.type = ScriptTokenType_OpBangEq},
            .expected = false,
        },
        {
            .a        = {.type = ScriptTokenType_LitNumber, .val_number = 42},
            .b        = {.type = ScriptTokenType_LitNumber, .val_number = 42},
            .expected = true,
        },
        {
            .a        = {.type = ScriptTokenType_LitNumber, .val_number = 42},
            .b        = {.type = ScriptTokenType_LitNumber, .val_number = 41},
            .expected = false,
        },
        {
            .a        = {.type = ScriptTokenType_LitBool, .val_bool = true},
            .b        = {.type = ScriptTokenType_LitBool, .val_bool = true},
            .expected = true,
        },
        {
            .a        = {.type = ScriptTokenType_LitBool, .val_bool = true},
            .b        = {.type = ScriptTokenType_LitBool, .val_bool = false},
            .expected = false,
        },
        {
            .a        = {.type = ScriptTokenType_LitKey, .val_key = string_hash_lit("HelloWorld")},
            .b        = {.type = ScriptTokenType_LitKey, .val_key = string_hash_lit("HelloWorld")},
            .expected = true,
        },
        {
            .a        = {.type = ScriptTokenType_LitKey, .val_key = string_hash_lit("Hello")},
            .b        = {.type = ScriptTokenType_LitKey, .val_key = string_hash_lit("HelloWorld")},
            .expected = false,
        },
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      if (testData[i].expected) {
        check_msg(
            script_token_equal(&testData[i].a, &testData[i].b),
            "{} == {}",
            script_token_fmt(&testData[i].a),
            script_token_fmt(&testData[i].b));
      } else {
        check_msg(
            !script_token_equal(&testData[i].a, &testData[i].b),
            "{} != {}",
            script_token_fmt(&testData[i].a),
            script_token_fmt(&testData[i].b));
      }
    }
  }

  it("can identify tokens") {
    const struct {
      String      input;
      ScriptToken expected;
    } testData[] = {
        {string_static("=="), {.type = ScriptTokenType_OpEqEq}},
        {string_static("!="), {.type = ScriptTokenType_OpBangEq}},
        {string_static("<"), {.type = ScriptTokenType_OpLe}},
        {string_static("<="), {.type = ScriptTokenType_OpLeEq}},
        {string_static(">"), {.type = ScriptTokenType_OpGt}},
        {string_static(">="), {.type = ScriptTokenType_OpGtEq}},
        {string_static("null"), {.type = ScriptTokenType_LitNull}},
        {string_static("42"), {.type = ScriptTokenType_LitNumber, .val_number = 42}},
        {string_static("true"), {.type = ScriptTokenType_LitBool, .val_bool = true}},
        {string_static("false"), {.type = ScriptTokenType_LitBool, .val_bool = false}},
        {
            string_static("$hello"),
            {.type = ScriptTokenType_LitKey, .val_key = string_hash_lit("hello")},
        },
        {string_static("|"), {.type = ScriptTokenType_Error, .val_error = ScriptError_InvalidChar}},
        {string_static(""), {.type = ScriptTokenType_End}},
    };

    for (u32 i = 0; i != array_elems(testData); ++i) {
      ScriptToken  token;
      const String rem = script_lex(testData[i].input, null, &token);

      check_msg(string_is_empty(rem), "Unexpected remaining input: '{}'", fmt_text(rem));
      check_msg(
          script_token_equal(&token, &testData[i].expected),
          "{} == {}",
          script_token_fmt(&token),
          script_token_fmt(&testData[i].expected));
    }
  }
}
