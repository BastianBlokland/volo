#include "core_array.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_math.h"
#include "core_utf8.h"
#include "script_lex.h"

#define script_token_err(_ERR_)                                                                    \
  (ScriptToken) { .type = ScriptTokenType_Error, .val_error = (_ERR_) }

typedef struct {
  String          id;
  ScriptTokenType token;
} ScriptLexKeyword;

static const ScriptLexKeyword g_lexKeywords[] = {
    {.id = string_static("if"), .token = ScriptTokenType_If},
    {.id = string_static("else"), .token = ScriptTokenType_Else},
    {.id = string_static("var"), .token = ScriptTokenType_Var},
    {.id = string_static("while"), .token = ScriptTokenType_While},
    {.id = string_static("continue"), .token = ScriptTokenType_Continue},
    {.id = string_static("break"), .token = ScriptTokenType_Break},
    {.id = string_static("for"), .token = ScriptTokenType_For},
};

static bool script_is_word_start(const u8 c) {
  // Either ascii letter or start of non-ascii utf8 character.
  static const u8 g_utf8Start = 0xC0;
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= g_utf8Start;
}

static bool script_is_word_separator(const u8 c) {
  switch (c) {
  case '\0':
  case '\t':
  case '\n':
  case '\r':
  case ' ':
  case '!':
  case '"':
  case '#':
  case '$':
  case '%':
  case '&':
  case '(':
  case ')':
  case '*':
  case '+':
  case ',':
  case '-':
  case '.':
  case '/':
  case ':':
  case ';':
  case '<':
  case '=':
  case '>':
  case '?':
  case '@':
  case '[':
  case '\\':
  case ']':
  case '^':
  case '`':
  case '{':
  case '|':
  case '}':
  case '~':
    return true;
  default:
    return false;
  }
}

static bool script_is_string_end(const u8 c) {
  switch (c) {
  case '\0':
  case '\n':
  case '\r':
  case '"':
    return true;
  default:
    return false;
  }
}

static u32 script_scan_word_end(const String str) {
  u32 end = 0;
  for (; end != str.size && !script_is_word_separator(*string_at(str, end)); ++end)
    ;
  return end;
}

static u32 script_scan_string_end(const String str) {
  u32 end = 0;
  for (; end != str.size && !script_is_string_end(*string_at(str, end)); ++end)
    ;
  return end;
}

static u32 script_scan_line_end(const String str) {
  u32 end = 0;
  for (; end != str.size && *string_at(str, end) != '\n'; ++end)
    ;
  return end;
}

static u32 script_scan_block_comment_end(const String str) {
  for (u32 i = 0; (i + 1) < str.size; ++i) {
    if (*string_at(str, i) == '*' && *string_at(str, i + 1) == '/') {
      return i + 2;
    }
  }
  return (u32)str.size;
}

static String script_consume_word_or_char(const String str) {
  diag_assert(!string_is_empty(str));
  return string_consume(str, math_max(script_scan_word_end(str), 1));
}

static u8 script_peek(const String str, const u32 ahead) {
  return UNLIKELY(str.size <= ahead) ? '\0' : *string_at(str, ahead);
}

static String script_lex_number_positive(String str, ScriptToken* out) {
  out->type = ScriptTokenType_Number;
  return format_read_f64(str, &out->val_number);
}

static String script_lex_key(String str, StringTable* stringtable, ScriptToken* out) {
  diag_assert(*string_begin(str) == '$');
  str = string_consume(str, 1); // Skip the leading '$'.

  const u32 end = script_scan_word_end(str);
  if (UNLIKELY(!end)) {
    *out = script_token_err(ScriptError_KeyEmpty);
    return str;
  }

  const String key = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(key))) {
    *out = script_token_err(ScriptError_InvalidUtf8);
    return str;
  }
  const StringHash keyHash = stringtable ? stringtable_add(stringtable, key) : string_hash(key);

  out->type    = ScriptTokenType_Key;
  out->val_key = keyHash;
  return string_consume(str, end);
}

static String script_lex_string(String str, StringTable* stringtable, ScriptToken* out) {
  diag_assert(*string_begin(str) == '"');
  str = string_consume(str, 1); // Skip the leading '"'.

  const u32 end = script_scan_string_end(str);
  if (UNLIKELY(end == str.size || *string_at(str, end) != '"')) {
    *out = script_token_err(ScriptError_UnterminatedString);
    return str;
  }

  const String val = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(val))) {
    *out = script_token_err(ScriptError_InvalidUtf8);
    return str;
  }
  const StringHash valHash = stringtable ? stringtable_add(stringtable, val) : string_hash(val);

  out->type       = ScriptTokenType_String;
  out->val_string = valHash;
  return string_consume(str, end + 1); // + 1 for the closing '"'.
}

static String script_lex_identifier(String str, ScriptToken* out) {
  const u32 end = script_scan_word_end(str);
  diag_assert(end);

  const String identifier = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(identifier))) {
    *out = script_token_err(ScriptError_InvalidUtf8);
    return str;
  }

  array_for_t(g_lexKeywords, ScriptLexKeyword, keyword) {
    if (string_eq(identifier, keyword->id)) {
      return out->type = keyword->token, string_consume(str, end);
    }
  }

  out->type           = ScriptTokenType_Identifier;
  out->val_identifier = string_hash(identifier);
  return string_consume(str, end);
}

String script_lex(String str, StringTable* stringtable, ScriptToken* out, const ScriptLexFlags fl) {
  while (!string_is_empty(str)) {
    const u8 c = string_begin(str)[0];
    switch (c) {
    case '(':
      return out->type = ScriptTokenType_ParenOpen, string_consume(str, 1);
    case ')':
      return out->type = ScriptTokenType_ParenClose, string_consume(str, 1);
    case '{':
      return out->type = ScriptTokenType_CurlyOpen, string_consume(str, 1);
    case '}':
      return out->type = ScriptTokenType_CurlyClose, string_consume(str, 1);
    case ',':
      return out->type = ScriptTokenType_Comma, string_consume(str, 1);
    case '=':
      if (script_peek(str, 1) == '=') {
        return out->type = ScriptTokenType_EqEq, string_consume(str, 2);
      }
      return out->type = ScriptTokenType_Eq, string_consume(str, 1);
    case '!':
      if (script_peek(str, 1) == '=') {
        return out->type = ScriptTokenType_BangEq, string_consume(str, 2);
      }
      return out->type = ScriptTokenType_Bang, string_consume(str, 1);
    case '<':
      if (script_peek(str, 1) == '=') {
        return out->type = ScriptTokenType_LeEq, string_consume(str, 2);
      }
      return out->type = ScriptTokenType_Le, string_consume(str, 1);
    case '>':
      if (script_peek(str, 1) == '=') {
        return out->type = ScriptTokenType_GtEq, string_consume(str, 2);
      }
      return out->type = ScriptTokenType_Gt, string_consume(str, 1);
    case ':':
      return out->type = ScriptTokenType_Colon, string_consume(str, 1);
    case ';':
      return out->type = ScriptTokenType_Semicolon, string_consume(str, 1);
    case '+':
      if (script_peek(str, 1) == '=') {
        return out->type = ScriptTokenType_PlusEq, string_consume(str, 2);
      }
      return out->type = ScriptTokenType_Plus, string_consume(str, 1);
    case '-':
      if (script_peek(str, 1) == '=') {
        return out->type = ScriptTokenType_MinusEq, string_consume(str, 2);
      }
      return out->type = ScriptTokenType_Minus, string_consume(str, 1);
    case '*':
      if (script_peek(str, 1) == '=') {
        return out->type = ScriptTokenType_StarEq, string_consume(str, 2);
      }
      return out->type = ScriptTokenType_Star, string_consume(str, 1);
    case '/':
      if (script_peek(str, 1) == '=') {
        return out->type = ScriptTokenType_SlashEq, string_consume(str, 2);
      }
      if (script_peek(str, 1) == '/') {
        str = string_consume(str, script_scan_line_end(str)); // Consume line comment.
        if (fl & ScriptLexFlags_IncludeComments) {
          return out->type = ScriptTokenType_Comment, str;
        }
        continue;
      }
      if (script_peek(str, 1) == '*') {
        str = string_consume(str, script_scan_block_comment_end(str)); // Consume block comment.
        if (fl & ScriptLexFlags_IncludeComments) {
          return out->type = ScriptTokenType_Comment, str;
        }
        continue;
      }
      return out->type = ScriptTokenType_Slash, string_consume(str, 1);
    case '%':
      if (script_peek(str, 1) == '=') {
        return out->type = ScriptTokenType_PercentEq, string_consume(str, 2);
      }
      return out->type = ScriptTokenType_Percent, string_consume(str, 1);
    case '&':
      if (script_peek(str, 1) == '&') {
        return out->type = ScriptTokenType_AmpAmp, string_consume(str, 2);
      }
      return *out = script_token_err(ScriptError_InvalidChar), string_consume(str, 1);
    case '|':
      if (script_peek(str, 1) == '|') {
        return out->type = ScriptTokenType_PipePipe, string_consume(str, 2);
      }
      return *out = script_token_err(ScriptError_InvalidChar), string_consume(str, 1);
    case '?':
      if (script_peek(str, 1) == '?') {
        if (script_peek(str, 2) == '=') {
          return out->type = ScriptTokenType_QMarkQMarkEq, string_consume(str, 3);
        }
        return out->type = ScriptTokenType_QMarkQMark, string_consume(str, 2);
      }
      return out->type = ScriptTokenType_QMark, string_consume(str, 1);
    case '.':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return script_lex_number_positive(str, out);
    case '$':
      return script_lex_key(str, stringtable, out);
    case '"':
      return script_lex_string(str, stringtable, out);
    case '\n':
      if (fl & ScriptLexFlags_IncludeNewlines) {
        return out->type = ScriptTokenType_Newline, string_consume(str, 1);
      }
      // Fallthrough.
    case ' ':
    case '\r':
    case '\t':
      str = string_consume(str, 1); // Skip whitespace.
      continue;
    default:
      if (script_is_word_start(c)) {
        return script_lex_identifier(str, out);
      }
      return *out = script_token_err(ScriptError_InvalidChar), script_consume_word_or_char(str);
    }
  }

  return out->type = ScriptTokenType_End, string_empty;
}

String script_lex_trim(String str) {
  while (!string_is_empty(str)) {
    const u8 c = string_begin(str)[0];
    switch (c) {
    case '/': {
      if (script_peek(str, 1) == '/') {
        str = string_consume(str, script_scan_line_end(str)); // Skip line comment.
        continue;
      }
      if (script_peek(str, 1) == '*') {
        str = string_consume(str, script_scan_block_comment_end(str)); // Skip block comment.
        continue;
      }
      return str;
    }
    case ' ':
    case '\n':
    case '\r':
    case '\t':
      str = string_consume(str, 1); // Skip whitespace.
      continue;
    default:
      return str;
    }
  }
  return string_empty;
}

bool script_token_equal(const ScriptToken* a, const ScriptToken* b) {
  if (a->type != b->type) {
    return false;
  }
  switch (a->type) {
  case ScriptTokenType_Number:
    return a->val_number == b->val_number;
  case ScriptTokenType_Identifier:
    return a->val_identifier == b->val_identifier;
  case ScriptTokenType_Key:
    return a->val_key == b->val_key;
  case ScriptTokenType_String:
    return a->val_string == b->val_string;
  case ScriptTokenType_Error:
    return a->val_error == b->val_error;
  default:
    return true;
  }
}

String script_token_str_scratch(const ScriptToken* token) {
  switch (token->type) {
  case ScriptTokenType_ParenOpen:
    return string_lit("(");
  case ScriptTokenType_ParenClose:
    return string_lit(")");
  case ScriptTokenType_CurlyOpen:
    return string_lit("{");
  case ScriptTokenType_CurlyClose:
    return string_lit("}");
  case ScriptTokenType_Comma:
    return string_lit(",");
  case ScriptTokenType_Eq:
    return string_lit("=");
  case ScriptTokenType_EqEq:
    return string_lit("==");
  case ScriptTokenType_Bang:
    return string_lit("!");
  case ScriptTokenType_BangEq:
    return string_lit("!=");
  case ScriptTokenType_Le:
    return string_lit("<");
  case ScriptTokenType_LeEq:
    return string_lit("<=");
  case ScriptTokenType_Gt:
    return string_lit(">");
  case ScriptTokenType_GtEq:
    return string_lit(">=");
  case ScriptTokenType_Plus:
    return string_lit("+");
  case ScriptTokenType_PlusEq:
    return string_lit("+=");
  case ScriptTokenType_Minus:
    return string_lit("-");
  case ScriptTokenType_MinusEq:
    return string_lit("-=");
  case ScriptTokenType_Star:
    return string_lit("*");
  case ScriptTokenType_StarEq:
    return string_lit("*=");
  case ScriptTokenType_Slash:
    return string_lit("/");
  case ScriptTokenType_SlashEq:
    return string_lit("/=");
  case ScriptTokenType_Percent:
    return string_lit("%");
  case ScriptTokenType_PercentEq:
    return string_lit("%=");
  case ScriptTokenType_Colon:
    return string_lit(":");
  case ScriptTokenType_Semicolon:
    return string_lit(";");
  case ScriptTokenType_AmpAmp:
    return string_lit("&&");
  case ScriptTokenType_PipePipe:
    return string_lit("||");
  case ScriptTokenType_QMark:
    return string_lit("?");
  case ScriptTokenType_QMarkQMark:
    return string_lit("??");
  case ScriptTokenType_QMarkQMarkEq:
    return string_lit("?\?=");
  case ScriptTokenType_Number:
    return fmt_write_scratch("{}", fmt_float(token->val_number));
  case ScriptTokenType_Identifier:
    return fmt_write_scratch("{}", fmt_int(token->val_identifier, .base = 16));
  case ScriptTokenType_Key:
    return fmt_write_scratch("${}", fmt_int(token->val_key, .base = 16));
  case ScriptTokenType_String:
    return fmt_write_scratch("#{}", fmt_int(token->val_string, .base = 16));
  case ScriptTokenType_If:
    return string_lit("if");
  case ScriptTokenType_Else:
    return string_lit("else");
  case ScriptTokenType_Var:
    return string_lit("var");
  case ScriptTokenType_While:
    return string_lit("while");
  case ScriptTokenType_For:
    return string_lit("for");
  case ScriptTokenType_Continue:
    return string_lit("continue");
  case ScriptTokenType_Break:
    return string_lit("break");
  case ScriptTokenType_Comment:
    return string_lit("comment");
  case ScriptTokenType_Newline:
    return string_lit("newline");
  case ScriptTokenType_Error:
    return script_error_str(token->val_error);
  case ScriptTokenType_End:
    return string_lit("\0");
  }
  diag_assert_fail("Unknown token-type");
  UNREACHABLE
}
