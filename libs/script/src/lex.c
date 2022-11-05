#include "core_diag.h"
#include "core_format.h"
#include "core_math.h"
#include "core_utf8.h"
#include "script_lex.h"

#define script_token_err(_ERR_)                                                                    \
  (ScriptToken) { .type = ScriptTokenType_Error, .val_error = (_ERR_) }

static bool script_is_word_start(const u8 c) {
  // Either ascii letter or start of non-ascii utf8 character.
  static const u8 g_utf8Start = 0xC0;
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= g_utf8Start;
}

static bool script_is_word_seperator(const u8 c) {
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

static u32 script_scan_word_end(const String str) {
  u32 end = 0;
  for (; end < str.size && !script_is_word_seperator(*string_at(str, end)); ++end)
    ;
  return end;
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

static String script_lex_identifier(String str, ScriptToken* out) {
  const u32 end = script_scan_word_end(str);
  diag_assert(end);

  const String identifier = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(identifier))) {
    *out = script_token_err(ScriptError_InvalidUtf8);
    return str;
  }

  out->type           = ScriptTokenType_Identifier;
  out->val_identifier = string_hash(identifier);
  return string_consume(str, end);
}

String script_lex(String str, StringTable* stringtable, ScriptToken* out) {
  while (!string_is_empty(str)) {
    const u8 c = string_begin(str)[0];
    switch (c) {
    case '(':
      return out->type = ScriptTokenType_ParenOpen, string_consume(str, 1);
    case ')':
      return out->type = ScriptTokenType_ParenClose, string_consume(str, 1);
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
    case ';':
      return out->type = ScriptTokenType_SemiColon, string_consume(str, 1);
    case '+':
      return out->type = ScriptTokenType_Plus, string_consume(str, 1);
    case '-':
      return out->type = ScriptTokenType_Minus, string_consume(str, 1);
    case '*':
      return out->type = ScriptTokenType_Star, string_consume(str, 1);
    case '/':
      return out->type = ScriptTokenType_Slash, string_consume(str, 1);
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
    case ' ':
    case '\n':
    case '\r':
    case '\t':
    case '\0':
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
  case ScriptTokenType_Minus:
    return string_lit("-");
  case ScriptTokenType_Star:
    return string_lit("*");
  case ScriptTokenType_Slash:
    return string_lit("/");
  case ScriptTokenType_SemiColon:
    return string_lit(";");
  case ScriptTokenType_AmpAmp:
    return string_lit("&&");
  case ScriptTokenType_PipePipe:
    return string_lit("||");
  case ScriptTokenType_QMark:
    return string_lit("?");
  case ScriptTokenType_QMarkQMark:
    return string_lit("??");
  case ScriptTokenType_Number:
    return fmt_write_scratch("{}", fmt_float(token->val_number));
  case ScriptTokenType_Identifier:
    return fmt_write_scratch("${}", fmt_int(token->val_identifier, .base = 16));
  case ScriptTokenType_Key:
    return fmt_write_scratch("${}", fmt_int(token->val_key, .base = 16));
  case ScriptTokenType_Error:
    return script_error_str(token->val_error);
  case ScriptTokenType_End:
    return string_lit("\0");
  }
  diag_assert_fail("Unknown token-type");
  UNREACHABLE
}
