#include "core_bits.h"
#include "core_diag.h"
#include "core_utf8.h"

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

static bool xml_is_name_start(const u8 ch) {
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

static bool xml_is_name(const u8 ch) {
  if (xml_is_name_start(ch)) {
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

static bool xml_is_string_end(const u8 c) {
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

static bool xml_is_content_end(const u8 c) {
  switch (c) {
  case '\0':
  case '<':
    return true;
  default:
    return false;
  }
}

static u32 xml_scan_name_end(const String str) {
  u32 end = 0;
  for (; end != str.size && !xml_is_name(*string_at(str, end)); ++end)
    ;
  return end;
}

static u32 xml_scan_string_end(const String str) {
  u32 end = 0;
  for (; end != str.size && !xml_is_string_end(*string_at(str, end)); ++end)
    ;
  return end;
}

static u32 xml_scan_comment_end(const String str) {
  u32 end = 0;
  while (end != str.size) {
    if (*string_at(str, end) == '-' && xml_peek(str, end + 1) == '-') {
      break;
    }
    ++end;
  }
  return end;
}

static u32 xml_scan_content_end(const String str) {
  u32 end = 0;
  for (; end != str.size && !xml_is_content_end(*string_at(str, end)); ++end)
    ;
  return end;
}

static String xml_lex_decl_start(String str, XmlToken* out) {
  diag_assert(string_begin(str)[0] == '<');
  diag_assert(string_begin(str)[1] == '?');
  str = xml_consume_chars(str, 2); // Skip the leading '<?'.

  if (string_is_empty(str) || !xml_is_name(string_begin(str)[0])) {
    return *out = xml_token_err(XmlError_InvalidDeclStart), str;
  }

  const u32 nameEnd = xml_scan_name_end(str);
  diag_assert(nameEnd != 0);

  out->type     = XmlTokenType_DeclStart;
  out->val_decl = string_slice(str, 0, nameEnd);

  return xml_consume_chars(str, nameEnd);
}

static String xml_lex_tag_start(String str, XmlToken* out) {
  diag_assert(string_begin(str)[0] == '<');
  str = xml_consume_chars(str, 1); // Skip the leading '<'.

  if (string_is_empty(str) || !xml_is_name(string_begin(str)[0])) {
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

  if (string_is_empty(str) || !xml_is_name(string_begin(str)[0])) {
    return *out = xml_token_err(XmlError_InvalidTagEnd), str;
  }

  const u32 nameEnd = xml_scan_name_end(str);
  diag_assert(nameEnd != 0);

  if (xml_peek(str, nameEnd) != '>') {
    return *out = xml_token_err(XmlError_InvalidTagEnd), xml_consume_chars(str, nameEnd);
  }

  out->type    = XmlTokenType_TagEnd;
  out->val_tag = string_slice(str, 0, nameEnd);

  return xml_consume_chars(str, nameEnd + 1); // + 1 for the closing '>'.
}

static String xml_lex_string(String str, XmlToken* out) {
  diag_assert(string_begin(str)[0] == '"');
  str = xml_consume_chars(str, 1); // Skip the leading '"'.

  const u32 end = xml_scan_string_end(str);
  if (xml_peek(str, end) != '=') {
    return *out = xml_token_err(XmlError_UnterminatedString), str;
  }

  const String val = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(val))) {
    return *out = xml_token_err(XmlError_InvalidUtf8), xml_consume_chars(str, end);
  }

  out->type       = XmlTokenType_String;
  out->val_string = string_slice(str, 0, end);

  return xml_consume_chars(str, end + 1); // + 1 for the closing '"'.
}

static String xml_lex_name(const String str, XmlToken* out) {
  const u32 end = xml_scan_name_end(str);
  diag_assert(end);

  const String id = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(id))) {
    return *out = xml_token_err(XmlError_InvalidUtf8), xml_consume_chars(str, end);
  }

  out->type     = XmlTokenType_Name;
  out->val_name = string_slice(str, 0, end);

  return xml_consume_chars(str, end);
}

static String xml_lex_comment(String str, XmlToken* out) {
  diag_assert(string_begin(str)[0] == '<');
  diag_assert(string_begin(str)[1] == '!');
  diag_assert(string_begin(str)[2] == '-');
  diag_assert(string_begin(str)[3] == '-');
  str = xml_consume_chars(str, 4); // Skip the leading '<!--'.

  const u32 end = xml_scan_comment_end(str);
  if (xml_peek(str, end) != '-' || xml_peek(str, end + 1) != '-') {
    return *out = xml_token_err(XmlError_UnterminatedComment), str;
  }
  if (xml_peek(str, end + 2) != '>') {
    return *out = xml_token_err(XmlError_InvalidCommentTerminator), str;
  }

  const String comment = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(comment))) {
    return *out = xml_token_err(XmlError_InvalidUtf8), xml_consume_chars(str, end);
  }

  out->type        = XmlTokenType_Comment;
  out->val_comment = string_slice(str, 0, end);

  return xml_consume_chars(str, end + 3); // + 3 for the closing '-->'.
}

String xml_lex(String str, const XmlPhase phase, XmlToken* out) {
  if (phase == XmlPhase_Content) {
    const u32    contentEnd = xml_scan_content_end(str);
    const String content    = string_trim_whitespace(string_slice(str, 0, contentEnd));
    if (string_is_empty(content)) {
      goto PhaseMarkup;
    }
    if (UNLIKELY(!utf8_validate(content))) {
      return *out = xml_token_err(XmlError_InvalidUtf8), xml_consume_chars(str, contentEnd);
    }

    out->type        = XmlTokenType_Content;
    out->val_content = string_slice(str, 0, contentEnd);

    return xml_consume_chars(str, contentEnd);
  }

PhaseMarkup:
  while (!string_is_empty(str)) {
    const u8 c = string_begin(str)[0];
    switch (c) {
    case '<':
      if (xml_peek(str, 1) == '?') {
        return xml_lex_decl_start(str, out);
      }
      if (xml_peek(str, 1) == '/') {
        return xml_lex_tag_end(str, out);
      }
      if (xml_peek(str, 1) == '!' && xml_peek(str, 2) == '-' && xml_peek(str, 3) == '-') {
        return xml_lex_comment(str, out);
      }
      return xml_lex_tag_start(str, out);
    case '>':
      return out->type = XmlTokenType_TagClose, xml_consume_chars(str, 1);
    case '=':
      return out->type = XmlTokenType_Equal, xml_consume_chars(str, 1);
    case '"':
      return xml_lex_string(str, out);
    case ' ':
    case '\r':
    case '\t':
      str = xml_consume_chars(str, 1); // Skip whitespace.
      continue;
    case '?':
      if (xml_peek(str, 1) == '>') {
        return out->type = XmlTokenType_DeclClose, xml_consume_chars(str, 2);
      }
      goto InvalidChar;
    case '/':
      if (xml_peek(str, 1) == '>') {
        return out->type = XmlTokenType_TagEndClose, xml_consume_chars(str, 2);
      }
      goto InvalidChar;
    default:
      if (xml_is_name_start(c)) {
        return xml_lex_name(str, out);
      }
    InvalidChar:
      return *out = xml_token_err(XmlError_InvalidChar), xml_consume_chars(str, 1);
    }
  }

  return out->type = XmlTokenType_End, string_empty;
}
