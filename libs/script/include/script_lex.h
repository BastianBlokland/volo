#pragma once
#include "core_stringtable.h"
#include "script_diag.h"

typedef enum {
  ScriptTokenKind_ParenOpen,    // (
  ScriptTokenKind_ParenClose,   // )
  ScriptTokenKind_CurlyOpen,    // {
  ScriptTokenKind_CurlyClose,   // }
  ScriptTokenKind_Comma,        // ,
  ScriptTokenKind_Eq,           // =
  ScriptTokenKind_EqEq,         // ==
  ScriptTokenKind_Bang,         // !
  ScriptTokenKind_BangEq,       // !=
  ScriptTokenKind_Le,           // <
  ScriptTokenKind_LeEq,         // <=
  ScriptTokenKind_Gt,           // >
  ScriptTokenKind_GtEq,         // >=
  ScriptTokenKind_Plus,         // +
  ScriptTokenKind_PlusEq,       // +=
  ScriptTokenKind_Minus,        // -
  ScriptTokenKind_MinusEq,      // -=
  ScriptTokenKind_Star,         // *
  ScriptTokenKind_StarEq,       // *=
  ScriptTokenKind_Slash,        // /
  ScriptTokenKind_SlashEq,      // /=
  ScriptTokenKind_Percent,      // %
  ScriptTokenKind_PercentEq,    // %=
  ScriptTokenKind_Colon,        // :
  ScriptTokenKind_Semicolon,    // ;
  ScriptTokenKind_AmpAmp,       // &&
  ScriptTokenKind_PipePipe,     // ||
  ScriptTokenKind_QMark,        // ?
  ScriptTokenKind_QMarkQMark,   // ??
  ScriptTokenKind_QMarkQMarkEq, // ??=
  ScriptTokenKind_Number,       // 42.1337
  ScriptTokenKind_Identifier,   // foo
  ScriptTokenKind_Key,          // $bar
  ScriptTokenKind_String,       // "Hello World"
  ScriptTokenKind_If,           // if
  ScriptTokenKind_Else,         // else
  ScriptTokenKind_Var,          // var
  ScriptTokenKind_While,        // while
  ScriptTokenKind_For,          // for
  ScriptTokenKind_Continue,     // continue
  ScriptTokenKind_Break,        // break
  ScriptTokenKind_Return,       // return
  ScriptTokenKind_Newline,      // \n
  ScriptTokenKind_CommentLine,  // // Hello
  ScriptTokenKind_CommentBlock, // /* World */
  ScriptTokenKind_Diag,         //
  ScriptTokenKind_End,          // \0
} ScriptTokenKind;

typedef struct {
  ScriptTokenKind kind;
  union {
    f64            val_number;
    StringHash     val_identifier;
    StringHash     val_key;
    StringHash     val_string;
    ScriptDiagKind val_diag;
  };
} ScriptToken;

typedef enum {
  ScriptLexFlags_None,
  ScriptLexFlags_IncludeNewlines = 1 << 0,
  ScriptLexFlags_IncludeComments = 1 << 1,
} ScriptLexFlags;

typedef struct {
  String          id;
  StringHash      idHash;
  ScriptTokenKind token;
} ScriptLexKeyword;

/**
 * Read a single script token.
 * Returns the remaining input.
 * The token is written to the output pointer.
 *
 * NOTE: StringTable can optionally provided to store the text representations of keys.
 */
String script_lex(String, StringTable*, ScriptToken* out, ScriptLexFlags);

/**
 * Consume any whitespace until the next token.
 */
String script_lex_trim(String, ScriptLexFlags);

/**
 * Retrieve global keyword list.
 */
u32                     script_lex_keyword_count(void);
const ScriptLexKeyword* script_lex_keyword_data(void);

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
