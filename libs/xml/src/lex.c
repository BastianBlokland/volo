#include "core_bits.h"
#include "core_diag.h"

#include "lex_internal.h"

#define xml_token_err(_ERR_)                                                                       \
  (XmlToken) { .type = XmlTokenType_Error, .val_error = (_ERR_) }

static String xml_consume_chars(const String str, const usize amount) {
  return (String){
      .ptr  = bits_ptr_offset(str.ptr, amount),
      .size = str.size - amount,
  };
}

static u8 xml_peek(const String str, const u32 ahead) {
  return UNLIKELY(str.size <= ahead) ? '\0' : *string_at(str, ahead);
}

static bool xml_is_name_start_char(const u8 ch) {
  // NOTE: Only Ascii names are supported atm.
  if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
    return true;
  }
  switch (ch) {
  case ':':
  case '_':
    return true;
  default:
    return false;
  }
}

static bool xml_is_name_char(const u8 ch) {
  if (xml_is_name_start_char(ch)) {
    return true;
  }
  if (ch >= '0' && ch <= '9') {
    return true;
  }
  switch (ch) {
  case '-':
  case '.':
    return true;
  default:
    return false;
  }
}

static u32 xml_scan_name_end(const String str) {
  u32 end = 0;
  for (; end != str.size && !xml_is_name_char(*string_at(str, end)); ++end)
    ;
  return end;
}

static String xml_lex_tag_start(String str, XmlToken* out) {
  diag_assert(string_begin(str)[0] == '<');
  str = xml_consume_chars(str, 1); // Skip the leading '<'.

  if (string_is_empty(str) || !xml_is_name_char(string_begin(str)[0])) {
    return *out = xml_token_err(XmlError_InvalidTagStart), str;
  }

  const u32 nameEnd = xml_scan_name_end(str);
  diag_assert(nameEnd != 0);

  out->type    = XmlTokenType_TagStart;
  out->val_tag = string_slice(str, 0, nameEnd);

  return xml_consume_chars(str, nameEnd);
}

static String xml_lex_tag_end(String str, XmlToken* out) {
  diag_assert(string_begin(str)[0] == '<');
  diag_assert(string_begin(str)[1] == '/');
  str = xml_consume_chars(str, 2); // Skip the leading '</'.

  if (string_is_empty(str) || !xml_is_name_char(string_begin(str)[0])) {
    return *out = xml_token_err(XmlError_InvalidTagEnd), str;
  }

  const u32 nameEnd = xml_scan_name_end(str);
  diag_assert(nameEnd != 0);

  if (xml_peek(str, nameEnd) != '>') {
    return *out = xml_token_err(XmlError_InvalidTagEnd), str;
  }

  out->type    = XmlTokenType_TagEnd;
  out->val_tag = string_slice(str, 0, nameEnd);

  return xml_consume_chars(str, nameEnd + 1);
}

String xml_lex_markup(String str, XmlToken* out) {
  while (!string_is_empty(str)) {
    const u8 c = string_begin(str)[0];
    switch (c) {
    case '<':
      if (xml_peek(str, 1) == '/') {
        return xml_lex_tag_end(str, out);
      }
      return xml_lex_tag_start(str, out);
    case '>':
      return out->type = XmlTokenType_TagClose, xml_consume_chars(str, 1);
    case '=':
      return out->type = XmlTokenType_Equal, xml_consume_chars(str, 1);
    case '/':
      if (xml_peek(str, 1) == '>') {
        return out->type = XmlTokenType_TagEndClose, xml_consume_chars(str, 1);
      }
    default:
      return *out = xml_token_err(XmlError_InvalidChar), xml_consume_chars(str, 1);
    }
  }

  return out->type = XmlTokenType_End, string_empty;
}
