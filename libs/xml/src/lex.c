#include "core_bits.h"

#include "lex_internal.h"

#define xml_token_err(_ERR_)                                                                       \
  (XmlToken) { .type = XmlTokenType_Error, .val_error = (_ERR_) }

/**
 * Consume x Ascii characters.
 */
INLINE_HINT static String xml_consume_chars(const String str, const usize amount) {
  return (String){
      .ptr  = bits_ptr_offset(str.ptr, amount),
      .size = str.size - amount,
  };
}

String xml_lex(String str, XmlToken* out) {
  while (!string_is_empty(str)) {
    switch (*string_begin(str)) {
    default:
      *out = xml_token_err(XmlError_InvalidChar);
      return xml_consume_chars(str, 1);
    }
  }

  return out->type = XmlTokenType_End, string_empty;
}
