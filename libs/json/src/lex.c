#include "core_alloc.h"
#include "core_ascii.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_utf8.h"

#include "lex_internal.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

#define json_string_max_size (usize_kibibyte * 64)

#define json_token_err(_ERR_)                                                                      \
  (JsonToken) { .type = JsonTokenType_Error, .val_error = (_ERR_) }

/**
 * Consume x Ascii characters.
 */
INLINE_HINT static String json_consume_chars(const String str, const usize amount) {
  return (String){
      .ptr  = bits_ptr_offset(str.ptr, amount),
      .size = str.size - amount,
  };
}

static String json_lex_number(String str, JsonToken* out) {
  out->type = JsonTokenType_Number;
  return format_read_f64(str, &out->val_number);
}

static String json_lex_string(String str, JsonToken* out) {

  // Caller is responsible for checking that the first character is valid.
  diag_assert(*string_begin(str) == '"');
  str = json_consume_chars(str, 1);

#ifdef VOLO_SIMD
  /**
   * Fast path for simple strings (no escape sequences), if the fast path cannot handle the input it
   * will abort and let the slow path handle it.
   */
  const SimdVec quoteVec   = simd_vec_broadcast_u8('"');
  const SimdVec escapeVec  = simd_vec_broadcast_u8('\\');
  const SimdVec limLowVec  = simd_vec_broadcast_u8(32);
  const SimdVec limHighVec = simd_vec_broadcast_u8(126);
  for (String rem = str; rem.size >= 16; rem = json_consume_chars(rem, 16)) {
    const SimdVec charsVec = simd_vec_load_unaligned(rem.ptr);

    SimdVec invalidVec = simd_vec_eq_u8(charsVec, escapeVec);
    invalidVec         = simd_vec_or(invalidVec, simd_vec_less_u8(charsVec, limLowVec));
    invalidVec         = simd_vec_or(invalidVec, simd_vec_greater_u8(charsVec, limHighVec));
    if (simd_vec_mask_u8(invalidVec)) {
      break; // Not valid for the fast-path: abort.
    }

    const u32 eqMask = simd_vec_mask_u8(simd_vec_eq_u8(charsVec, quoteVec));
    if (eqMask) {
      rem             = json_consume_chars(rem, intrinsic_ctz_32(eqMask) + 1);
      out->type       = JsonTokenType_String;
      out->val_string = mem_from_to(str.ptr, (u8*)rem.ptr - 1);
      return rem;
    }
  }
#endif

  DynString result = dynstring_create_over(alloc_alloc(g_allocScratch, json_string_max_size, 1));

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
    str         = json_consume_chars(str, 1);

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
        utf8_cp_write_to(&result, (Unicode)unicode);
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
      out->val_string = mem_create(result.data.ptr, result.size);
      goto Ret;
    default:
      if (UNLIKELY(!ascii_is_printable(ch))) {
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
    return json_consume_chars(str, 4);
  }
  *out = json_token_err(JsonError_InvalidCharInTrue);
  return json_consume_chars(str, 1);
}

static String json_lex_false(String str, JsonToken* out) {
  if (LIKELY(string_starts_with(str, string_lit("false")))) {
    out->type = JsonTokenType_False;
    return json_consume_chars(str, 5);
  }
  *out = json_token_err(JsonError_InvalidCharInFalse);
  return json_consume_chars(str, 1);
}

static String json_lex_null(String str, JsonToken* out) {
  if (LIKELY(string_starts_with(str, string_lit("null")))) {
    out->type = JsonTokenType_Null;
    return json_consume_chars(str, 4);
  }
  *out = json_token_err(JsonError_InvalidCharInNull);
  return json_consume_chars(str, 1);
}

String json_lex(String str, JsonToken* out) {
  while (!string_is_empty(str)) {
    switch (*string_begin(str)) {
    case '[':
      out->type = JsonTokenType_BracketOpen;
      return json_consume_chars(str, 1);
    case ']':
      out->type = JsonTokenType_BracketClose;
      return json_consume_chars(str, 1);
    case '{':
      out->type = JsonTokenType_CurlyOpen;
      return json_consume_chars(str, 1);
    case '}':
      out->type = JsonTokenType_CurlyClose;
      return json_consume_chars(str, 1);
    case ',':
      out->type = JsonTokenType_Comma;
      return json_consume_chars(str, 1);
    case ':':
      out->type = JsonTokenType_Colon;
      return json_consume_chars(str, 1);
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
      str = json_consume_chars(str, 1);
      continue;
    default:
      *out = json_token_err(JsonError_InvalidChar);
      return json_consume_chars(str, 1);
    }
  }

  out->type = JsonTokenType_End;
  return string_empty;
}
