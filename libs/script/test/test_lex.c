#include "check_spec.h"
#include "core_array.h"
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
}
