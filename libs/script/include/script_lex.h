#pragma once
#include "core_stringtable.h"
#include "script_result.h"

typedef enum {
  ScriptTokenType_ParenOpen,    // (
  ScriptTokenType_ParenClose,   // )
  ScriptTokenType_CurlyOpen,    // {
  ScriptTokenType_CurlyClose,   // }
  ScriptTokenType_Comma,        // ,
  ScriptTokenType_Eq,           // =
  ScriptTokenType_EqEq,         // ==
  ScriptTokenType_Bang,         // !
  ScriptTokenType_BangEq,       // !=
  ScriptTokenType_Le,           // <
  ScriptTokenType_LeEq,         // <=
  ScriptTokenType_Gt,           // >
  ScriptTokenType_GtEq,         // >=
  ScriptTokenType_Plus,         // +
  ScriptTokenType_PlusEq,       // +=
  ScriptTokenType_Minus,        // -
  ScriptTokenType_MinusEq,      // -=
  ScriptTokenType_Star,         // *
  ScriptTokenType_StarEq,       // *=
  ScriptTokenType_Slash,        // /
  ScriptTokenType_SlashEq,      // /=
  ScriptTokenType_Percent,      // %
  ScriptTokenType_PercentEq,    // %=
  ScriptTokenType_Colon,        // :
  ScriptTokenType_SemiColon,    // ;
  ScriptTokenType_AmpAmp,       // &&
  ScriptTokenType_PipePipe,     // ||
  ScriptTokenType_QMark,        // ?
  ScriptTokenType_QMarkQMark,   // ??
  ScriptTokenType_QMarkQMarkEq, // ??=
  ScriptTokenType_Number,       // 42.1337
  ScriptTokenType_Identifier,   // foo
  ScriptTokenType_Key,          // $bar
  ScriptTokenType_String,       // "Hello World"
  ScriptTokenType_If,           // if
  ScriptTokenType_Else,         // else
  ScriptTokenType_Var,          // var
  ScriptTokenType_While,        // while
  ScriptTokenType_For,          // for
  ScriptTokenType_Continue,     // continue
  ScriptTokenType_Break,        // break
  ScriptTokenType_Comment,      // /* Hello */ or // World
  ScriptTokenType_Error,        //
  ScriptTokenType_End,          // \0
} ScriptTokenType;

typedef struct {
  ScriptTokenType type;
  union {
    f64          val_number;
    StringHash   val_identifier;
    StringHash   val_key;
    StringHash   val_string;
    ScriptResult val_error;
  };
} ScriptToken;

typedef enum {
  ScriptLexFlags_None,
  ScriptLexFlags_IncludeComments = 1 << 0,
} ScriptLexFlags;

/**
 * Read a single script token.
 * Returns the remaining input.
 * The token is written to the output pointer.
 *
 * NOTE: StringTable can optionally provided to store the text representations of keys.
 */
String script_lex(String, StringTable*, ScriptToken*, ScriptLexFlags);

/**
 * Consume any whitespace until the next token.
 */
String script_lex_trim(String);

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
