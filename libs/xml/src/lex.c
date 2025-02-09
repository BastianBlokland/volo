#include "core_alloc.h"
#include "core_ascii.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
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

static bool xml_is_string_single_end(const u8 c) {
  switch (c) {
  case '\0':
  case '\n':
  case '\r':
  case '\'':
    return true;
  default:
    return false;
  }
}

static bool xml_is_string_double_end(const u8 c) {
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
  for (; end != str.size && xml_is_name(*string_at(str, end)); ++end)
    ;
  return end;
}

static u32 xml_scan_string_single_end(const String str) {
  u32 end = 0;
  for (; end != str.size && !xml_is_string_single_end(*string_at(str, end)); ++end)
    ;
  return end;
}

static u32 xml_scan_string_double_end(const String str) {
  u32 end = 0;
  for (; end != str.size && !xml_is_string_double_end(*string_at(str, end)); ++end)
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

  if (UNLIKELY(string_is_empty(str) || !xml_is_name_start(string_begin(str)[0]))) {
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

  if (UNLIKELY(string_is_empty(str) || !xml_is_name_start(string_begin(str)[0]))) {
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

  if (UNLIKELY(string_is_empty(str) || !xml_is_name_start(string_begin(str)[0]))) {
    return *out = xml_token_err(XmlError_InvalidTagEnd), str;
  }

  const u32 nameEnd = xml_scan_name_end(str);
  diag_assert(nameEnd != 0);

  if (UNLIKELY(xml_peek(str, nameEnd) != '>')) {
    return *out = xml_token_err(XmlError_InvalidTagEnd), xml_consume_chars(str, nameEnd);
  }

  out->type    = XmlTokenType_TagEnd;
  out->val_tag = string_slice(str, 0, nameEnd);

  return xml_consume_chars(str, nameEnd + 1); // + 1 for the closing '>'.
}

static String xml_lex_string(String str, XmlToken* out) {
  const u8 term = string_begin(str)[0];
  diag_assert(term == '"' || term == '\'');
  str = xml_consume_chars(str, 1); // Skip the leading '"' or '\''.

  const u32 end = term == '\'' ? xml_scan_string_single_end(str) : xml_scan_string_double_end(str);
  if (UNLIKELY(xml_peek(str, end) != term)) {
    return *out = xml_token_err(XmlError_UnterminatedString), str;
  }

  const String val = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(val))) {
    return *out = xml_token_err(XmlError_InvalidUtf8), xml_consume_chars(str, end);
  }

  out->type       = XmlTokenType_String;
  out->val_string = string_slice(str, 0, end);

  return xml_consume_chars(str, end + 1); // + 1 for the closing '"' or '\''.
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
  if (UNLIKELY(xml_peek(str, end) != '-' || xml_peek(str, end + 1) != '-')) {
    return *out = xml_token_err(XmlError_UnterminatedComment), str;
  }
  if (UNLIKELY(xml_peek(str, end + 2) != '>')) {
    return *out = xml_token_err(XmlError_InvalidCommentTerminator), str;
  }

  const String comment = string_trim_whitespace(string_slice(str, 0, end));
  if (UNLIKELY(!utf8_validate(comment))) {
    return *out = xml_token_err(XmlError_InvalidUtf8), xml_consume_chars(str, end);
  }

  out->type        = XmlTokenType_Comment;
  out->val_comment = comment;

  return xml_consume_chars(str, end + 3); // + 3 for the closing '-->'.
}

static void xml_process_content(String content, XmlToken* out) {
  Mem       resultBuffer = alloc_alloc(g_allocScratch, alloc_max_size(g_allocScratch), 1);
  DynString result       = dynstring_create_over(resultBuffer);

  while (true) {
    if (UNLIKELY(result.size == resultBuffer.size)) {
      *out = xml_token_err(XmlError_ContentTooLong);
      break;
    }
    if (string_is_empty(content)) {
      out->type        = XmlTokenType_Content;
      out->val_content = dynstring_view(&result);
      break;
    }
    const u8 ch = string_begin(content)[0];
    if (ch == '&') {
      content = xml_consume_chars(content, 1);
      if (xml_peek(content, 1) == '#') {
        content = xml_consume_chars(content, 1); // Consume the '#'.
        u8 base = 10;
        if (xml_peek(content, 1) == 'x') {
          content = xml_consume_chars(content, 1); // Consume the 'x'.
          base    = 16;
        }
        u64 unicode;
        content = format_read_u64(content, &unicode, base);
        if (UNLIKELY(result.size + utf8_cp_bytes((Unicode)unicode) >= resultBuffer.size)) {
          *out = xml_token_err(XmlError_ContentTooLong);
          break;
        }
        if (UNLIKELY(unicode == 0 || xml_peek(content, 1) != ';')) {
          *out = xml_token_err(XmlError_InvalidReference);
          break;
        }
        content = xml_consume_chars(content, 1); // Consume the ';'.
        utf8_cp_write_to(&result, (Unicode)unicode);
        continue;
      }
      if (string_starts_with(content, string_lit("lt;"))) {
        dynstring_append_char(&result, '<');
        content = xml_consume_chars(content, 3); // Consume the 'lt;'.
        continue;
      }
      if (string_starts_with(content, string_lit("gt;"))) {
        dynstring_append_char(&result, '>');
        content = xml_consume_chars(content, 3); // Consume the 'gt;'.
        continue;
      }
      if (string_starts_with(content, string_lit("amp;"))) {
        dynstring_append_char(&result, '&');
        content = xml_consume_chars(content, 4); // Consume the 'amp;'.
        continue;
      }
      if (string_starts_with(content, string_lit("apos;"))) {
        dynstring_append_char(&result, '\'');
        content = xml_consume_chars(content, 5); // Consume the 'apos;'.
        continue;
      }
      if (string_starts_with(content, string_lit("quot;"))) {
        dynstring_append_char(&result, '"');
        content = xml_consume_chars(content, 5); // Consume the 'quot;'.
        continue;
      }
      *out = xml_token_err(XmlError_InvalidReference);
      break;
    }

    static const u8 g_utf8Start = 0xC0;
    if (ch >= g_utf8Start) {
      Unicode cp;
      content = utf8_cp_read(content, &cp);
      if (UNLIKELY(!cp)) {
        *out = xml_token_err(XmlError_InvalidUtf8);
        break;
      }
      if (UNLIKELY(result.size + utf8_cp_bytes(cp) >= resultBuffer.size)) {
        *out = xml_token_err(XmlError_ContentTooLong);
        break;
      }
      utf8_cp_write_to(&result, cp);
    } else {
      content = xml_consume_chars(content, 1);
      if (UNLIKELY(!ascii_is_printable(ch))) {
        *out = xml_token_err(XmlError_InvalidCharInContent);
        break;
      }
      dynstring_append_char(&result, ch);
    }
  }
}

String xml_lex(String str, const XmlPhase phase, XmlToken* out) {
  if (phase == XmlPhase_Content) {
    const u32    contentEnd = xml_scan_content_end(str);
    const String content    = string_trim_whitespace(string_slice(str, 0, contentEnd));
    if (string_is_empty(content)) {
      goto PhaseMarkup;
    }
    xml_process_content(content, out);
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
    case '\'':
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
