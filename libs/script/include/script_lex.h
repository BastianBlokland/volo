#pragma once
#include "core_stringtable.h"
#include "script_error.h"

typedef enum {
  ScriptTokenType_ParenOpen,  // (
  ScriptTokenType_ParenClose, // )
  ScriptTokenType_Comma,      // ,
  ScriptTokenType_Eq,         // =
  ScriptTokenType_EqEq,       // ==
  ScriptTokenType_Bang,       // !
  ScriptTokenType_BangEq,     // !=
  ScriptTokenType_Le,         // <
  ScriptTokenType_LeEq,       // <=
  ScriptTokenType_Gt,         // >
  ScriptTokenType_GtEq,       // >=
  ScriptTokenType_Plus,       // +
  ScriptTokenType_PlusEq,     // +=
  ScriptTokenType_Minus,      // -
  ScriptTokenType_MinusEq,    // -=
  ScriptTokenType_Star,       // *
  ScriptTokenType_Slash,      // /
  ScriptTokenType_Colon,      // :
  ScriptTokenType_SemiColon,  // ;
  ScriptTokenType_AmpAmp,     // &&
  ScriptTokenType_PipePipe,   // ||
  ScriptTokenType_QMark,      // ?
  ScriptTokenType_QMarkQMark, // ??
  ScriptTokenType_Number,     // 42.1337
  ScriptTokenType_Identifier, // foo
  ScriptTokenType_Key,        // $bar
  ScriptTokenType_Error,      //
  ScriptTokenType_End,        // \0
} ScriptTokenType;

typedef struct {
  ScriptTokenType type;
  union {
    f64         val_number;
    StringHash  val_identifier;
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
