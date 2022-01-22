#include "core_alloc.h"
#include "core_ascii.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_utf8.h"

#include "lex_internal.h"

#define json_string_max_size (usize_kibibyte * 64)

#define json_token_err(_ERR_)                                                                      \
  (JsonToken) { .type = JsonTokenType_Error, .val_error = (_ERR_) }

static String json_lex_number(String str, JsonToken* out) {
  out->type = JsonTokenType_Number;
  return format_read_f64(str, &out->val_number);
}

static String json_lex_string(String str, JsonToken* out) {

  // Caller is responsible for checking that the first character is valid.
  diag_assert(*string_begin(str) == '"');
  str = string_consume(str, 1);

  DynString result = dynstring_create_over(alloc_alloc(g_alloc_scratch, json_string_max_size, 1));

  bool escaped = false;
  while (true) {

    if (UNLIKELY(result.size >= json_string_max_size)) {
      *out = json_token_err(JsonError_TooLongString);
      goto Ret;
    }

    if (UNLIKELY(string_is_empty(str))) {
      *out = json_token_err(JsonError_UnterminatedString);
      goto Ret;
    }

    const u8 ch = *string_begin(str);
    str         = string_consume(str, 1);

    if (escaped) {
      switch (ch) {
      case '"':
        dynstring_append_char(&result, '"');
        break;
      case '\\':
        dynstring_append_char(&result, '\\');
        break;
      case '/':
        dynstring_append_char(&result, '/');
        break;
      case 'b':
        dynstring_append_char(&result, '\b');
        break;
      case 'f':
        dynstring_append_char(&result, '\f');
        break;
      case 'n':
        dynstring_append_char(&result, '\n');
        break;
      case 'r':
        dynstring_append_char(&result, '\r');
        break;
      case 't':
        dynstring_append_char(&result, '\t');
        break;
      case 'u':
      case 'U': {
        u64 unicode;
        str = format_read_u64(str, &unicode, 16);
        utf8_cp_write(&result, (Unicode)unicode);
      } break;
      default:
        *out = json_token_err(JsonError_InvalidEscapeSequence);
        goto Ret;
      }
      escaped = false;
      continue;
    }

    switch (ch) {
    case '\\':
      escaped = true;
      break;
    case '"':
      out->type       = JsonTokenType_String;
      out->val_string = dynstring_view(&result);
      goto Ret;
    default:
      if (UNLIKELY(ascii_is_control(ch))) {
        *out = json_token_err(JsonError_InvalidCharInString);
        goto Ret;
      }
      dynstring_append_char(&result, ch);
    }
  }

Ret:
  dynstring_destroy(&result);
  return str;
}

static String json_lex_true(String str, JsonToken* out) {
  if (LIKELY(string_starts_with(str, string_lit("true")))) {
    out->type = JsonTokenType_True;
    return string_consume(str, 4);
  }
  *out = json_token_err(JsonError_InvalidCharInTrue);
  return string_consume(str, 1);
}

static String json_lex_false(String str, JsonToken* out) {
  if (LIKELY(string_starts_with(str, string_lit("false")))) {
    out->type = JsonTokenType_False;
    return string_consume(str, 5);
  }
  *out = json_token_err(JsonError_InvalidCharInFalse);
  return string_consume(str, 1);
}

static String json_lex_null(String str, JsonToken* out) {
  if (LIKELY(string_starts_with(str, string_lit("null")))) {
    out->type = JsonTokenType_Null;
    return string_consume(str, 4);
  }
  *out = json_token_err(JsonError_InvalidCharInNull);
  return string_consume(str, 1);
}

String json_lex(String str, JsonToken* out) {
  while (!string_is_empty(str)) {
    switch (*string_begin(str)) {
    case '[':
      out->type = JsonTokenType_BracketOpen;
      return string_consume(str, 1);
    case ']':
      out->type = JsonTokenType_BracketClose;
      return string_consume(str, 1);
    case '{':
      out->type = JsonTokenType_CurlyOpen;
      return string_consume(str, 1);
    case '}':
      out->type = JsonTokenType_CurlyClose;
      return string_consume(str, 1);
    case ',':
      out->type = JsonTokenType_Comma;
      return string_consume(str, 1);
    case ':':
      out->type = JsonTokenType_Colon;
      return string_consume(str, 1);
    case '"':
      return json_lex_string(str, out);
    case 't':
      return json_lex_true(str, out);
    case 'f':
      return json_lex_false(str, out);
    case 'n':
      return json_lex_null(str, out);
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
      return json_lex_number(str, out);
    case ' ':
    case '\n':
    case '\r':
    case '\t':
      str = string_consume(str, 1);
      continue;
    default:
      *out = json_token_err(JsonError_InvalidChar);
      return string_consume(str, 1);
    }
  }

  out->type = JsonTokenType_End;
  return string_empty;
}
