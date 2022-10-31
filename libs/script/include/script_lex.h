#pragma once
#include "core_stringtable.h"
#include "script_error.h"

typedef enum {
  ScriptTokenType_SepParenOpen,  // (
  ScriptTokenType_SepParenClose, // )
  ScriptTokenType_OpEqEq,        // ==
  ScriptTokenType_OpBangEq,      // !=
  ScriptTokenType_OpLe,          // <
  ScriptTokenType_OpLeEq,        // <=
  ScriptTokenType_OpGt,          // >
  ScriptTokenType_OpGtEq,        // >=
  ScriptTokenType_OpPlus,        // +
  ScriptTokenType_OpMinus,       // -
  ScriptTokenType_LitNull,       // null
  ScriptTokenType_LitNumber,     // 42.1337
  ScriptTokenType_LitBool,       // true
  ScriptTokenType_LitKey,        // $foo
  ScriptTokenType_Error,         //
  ScriptTokenType_End,           // \0
} ScriptTokenType;

typedef struct {
  ScriptTokenType type;
  union {
    f64         val_number;
    bool        val_bool;
    StringHash  val_key;
    ScriptError val_error;
  };
} ScriptToken;

/**
 * Read a single scripts token.
 * Returns the remaining input.
 * The token is written to the output pointer.
 *
 * NOTE: StringTable can optionally provided to store the text representations of keys.
 */
String script_lex(String, StringTable*, ScriptToken*);

/**
 * Test if two tokens are equal.
 */
bool script_token_equal(const ScriptToken*, const ScriptToken*);

/**
 * Create a textual representation of the given token.
 * NOTE: Returned string is allocated in scratch memory.
 */
String script_token_str_scratch(const ScriptToken*);

/**
 * Create a formatting argument for a token.
 */
#define script_token_fmt(_TOKEN_) fmt_text(script_token_str_scratch(_TOKEN_))
