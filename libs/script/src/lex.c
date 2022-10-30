#include "core_diag.h"
#include "core_format.h"
#include "core_utf8.h"
#include "script_lex.h"

#define script_token_err(_ERR_)                                                                    \
  (ScriptToken) { .type = ScriptTokenType_Error, .val_error = (_ERR_) }

static bool script_is_seperator(const u8 c) {
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

static u8 script_peek(const String str, const u32 ahead) {
  return UNLIKELY(str.size <= ahead) ? '\0' : *string_at(str, ahead);
}

static String script_lex_null(String str, ScriptToken* out) {
  if (LIKELY(string_starts_with(str, string_lit("null")))) {
    out->type = ScriptTokenType_LitNull;
    return string_consume(str, 4);
  }
  *out = script_token_err(ScriptError_InvalidCharInNull);
  return string_consume(str, 1);
}

static String script_lex_lit_number(String str, ScriptToken* out) {
  out->type = ScriptTokenType_LitNumber;
  return format_read_f64(str, &out->val_number);
}

static String script_lex_true(String str, ScriptToken* out) {
  if (LIKELY(string_starts_with(str, string_lit("true")))) {
    out->type     = ScriptTokenType_LitBool;
    out->val_bool = true;
    return string_consume(str, 4);
  }
  *out = script_token_err(ScriptError_InvalidCharInTrue);
  return string_consume(str, 1);
}

static String script_lex_false(String str, ScriptToken* out) {
  if (LIKELY(string_starts_with(str, string_lit("false")))) {
    out->type     = ScriptTokenType_LitBool;
    out->val_bool = false;
    return string_consume(str, 5);
  }
  *out = script_token_err(ScriptError_InvalidCharInFalse);
  return string_consume(str, 1);
}

static String script_lex_key(String str, StringTable* stringtable, ScriptToken* out) {
  diag_assert(*string_begin(str) == '$');
  str = string_consume(str, 1); // Skip the leading '$'.

  u32 end = 0;
  for (; end < str.size && !script_is_seperator(*string_at(str, end)); ++end)
    ;
  if (UNLIKELY(!end)) {
    *out = script_token_err(ScriptError_KeyIdentifierEmpty);
    return str;
  }

  const String key = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(key))) {
    *out = script_token_err(ScriptError_KeyIdentifierInvalidUtf8);
    return str;
  }
  const StringHash keyHash = stringtable ? stringtable_add(stringtable, key) : string_hash(key);

  out->type    = ScriptTokenType_LitKey;
  out->val_key = keyHash;
  return string_consume(str, end);
}

String script_lex(String str, StringTable* stringtable, ScriptToken* out) {
  while (!string_is_empty(str)) {
    switch (*string_begin(str)) {
    case '=':
      if (script_peek(str, 1) == '=') {
        out->type = ScriptTokenType_OpEqEq;
        return string_consume(str, 2);
      }
      *out = script_token_err(ScriptError_InvalidChar);
      return string_consume(str, 1);
    case '!':
      if (script_peek(str, 1) == '=') {
        out->type = ScriptTokenType_OpBangEq;
        return string_consume(str, 2);
      }
      *out = script_token_err(ScriptError_InvalidChar);
      return string_consume(str, 1);
    case '<':
      if (script_peek(str, 1) == '=') {
        out->type = ScriptTokenType_OpLeEq;
        return string_consume(str, 2);
      }
      out->type = ScriptTokenType_OpLe;
      return string_consume(str, 1);
    case '>':
      if (script_peek(str, 1) == '=') {
        out->type = ScriptTokenType_OpGtEq;
        return string_consume(str, 2);
      }
      out->type = ScriptTokenType_OpGt;
      return string_consume(str, 1);
    case 'n':
      return script_lex_null(str, out);
    case '-':
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
      return script_lex_lit_number(str, out);
    case 't':
      return script_lex_true(str, out);
    case 'f':
      return script_lex_false(str, out);
    case '$':
      return script_lex_key(str, stringtable, out);
    case ' ':
    case '\n':
    case '\r':
    case '\t':
      str = string_consume(str, 1); // Skip whitespace.
      continue;
    default:
      *out = script_token_err(ScriptError_InvalidChar);
      return string_consume(str, 1);
    }
  }

  out->type = ScriptTokenType_End;
  return string_empty;
}

String script_token_str(const ScriptToken* token) {
  switch (token->type) {
  case ScriptTokenType_OpEqEq:
    return string_lit("==");
  case ScriptTokenType_OpBangEq:
    return string_lit("!=");
  case ScriptTokenType_OpLe:
    return string_lit("<");
  case ScriptTokenType_OpLeEq:
    return string_lit("<=");
  case ScriptTokenType_OpGt:
    return string_lit(">");
  case ScriptTokenType_OpGtEq:
    return string_lit(">=");
  case ScriptTokenType_LitNull:
    return string_lit("null");
  case ScriptTokenType_LitNumber:
    return fmt_write_scratch("{}", fmt_float(token->val_number));
  case ScriptTokenType_LitBool:
    return fmt_write_scratch("{}", fmt_bool(token->val_bool));
  case ScriptTokenType_LitKey:
    return fmt_write_scratch("${}", fmt_int(token->val_key, .base = 16));
  case ScriptTokenType_Error:
    return script_error_str(token->val_error);
  case ScriptTokenType_End:
    return string_lit("\0");
  }
  diag_crash_msg("Unknown token-type");
}
