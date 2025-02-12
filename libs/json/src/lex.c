#include "core_alloc.h"
#include "core_ascii.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_format.h"
#include "core_utf8.h"

#include "lex_internal.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

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
   * Fast path for simple Ascii strings (with no escape sequences), if the fast path cannot handle
   * the input it will abort and let the slow path handle it.
   */
  const SimdVec quoteVec   = simd_vec_broadcast_u8('"');
  const SimdVec escapeVec  = simd_vec_broadcast_u8('\\');
  const SimdVec limLowVec  = simd_vec_broadcast_u8(32);
  const SimdVec limHighVec = simd_vec_broadcast_u8(126);
  for (String rem = str; rem.size >= 16; rem = json_consume_chars(rem, 16)) {
    const SimdVec charsVec = simd_vec_load_unaligned(rem.ptr);

    SimdVec invalidVec    = simd_vec_eq_u8(charsVec, escapeVec);
    invalidVec            = simd_vec_or(invalidVec, simd_vec_less_i8(charsVec, limLowVec));
    invalidVec            = simd_vec_or(invalidVec, simd_vec_greater_i8(charsVec, limHighVec));
    const u32 invalidMask = simd_vec_mask_u8(invalidVec);

    const u32 eqMask = simd_vec_mask_u8(simd_vec_eq_u8(charsVec, quoteVec));
    if (eqMask) {
      const u32 eqIndex = intrinsic_ctz_32(eqMask);
      if (invalidMask && intrinsic_ctz_32(invalidMask) <= eqIndex) {
        break; // A character inside the string was not valid for the fast-path: abort.
      }
      rem             = json_consume_chars(rem, eqIndex + 1);
      out->type       = JsonTokenType_String;
      out->val_string = mem_from_to(str.ptr, (u8*)rem.ptr - 1);
      return rem;
    }
    if (invalidMask) {
      break; // Any character was not valid for the fast-path: abort.
    }
  }
#endif

  Mem       resultBuffer = alloc_alloc(g_allocScratch, alloc_max_size(g_allocScratch), 1);
  DynString result       = dynstring_create_over(resultBuffer);

  bool escaped = false;
  while (true) {
    if (UNLIKELY(result.size == resultBuffer.size)) {
      *out = json_token_err(JsonError_TooLongString);
      goto Ret;
    }
    if (UNLIKELY(string_is_empty(str))) {
      *out = json_token_err(JsonError_UnterminatedString);
      goto Ret;
    }
    const u8 ch = *string_begin(str);
    if (escaped) {
      str = json_consume_chars(str, 1);
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
        if (UNLIKELY(result.size + utf8_cp_bytes((Unicode)unicode) >= resultBuffer.size)) {
          *out = json_token_err(JsonError_TooLongString);
          goto Ret;
        }
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
      str     = json_consume_chars(str, 1);
      escaped = true;
      break;
    case '"':
      str             = json_consume_chars(str, 1);
      out->type       = JsonTokenType_String;
      out->val_string = mem_create(result.data.ptr, result.size);
      goto Ret;
    default:;
      static const u8 g_utf8Start = 0xC0;
      if (ch >= g_utf8Start) {
        Unicode cp;
        str = utf8_cp_read(str, &cp);
        if (UNLIKELY(!cp)) {
          *out = json_token_err(JsonError_InvalidUtf8);
          goto Ret;
        }
        if (UNLIKELY(result.size + utf8_cp_bytes(cp) >= resultBuffer.size)) {
          *out = json_token_err(JsonError_TooLongString);
          goto Ret;
        }
        utf8_cp_write_to(&result, cp);
      } else {
        str = json_consume_chars(str, 1);
        if (UNLIKELY(!ascii_is_printable(ch))) {
          *out = json_token_err(JsonError_InvalidCharInString);
          goto Ret;
        }
        dynstring_append_char(&result, ch);
      }
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
