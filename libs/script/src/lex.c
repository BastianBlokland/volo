#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_utf8.h"
#include "script_lex.h"

#define script_token_diag(_DIAG_)                                                                  \
  (ScriptToken) { .kind = ScriptTokenKind_Diag, .val_diag = (_DIAG_) }

INLINE_HINT static String script_consume_chars(const String str, const usize amount) {
  return (String){
      .ptr  = bits_ptr_offset(str.ptr, amount),
      .size = str.size - amount,
  };
}

static ScriptLexKeyword g_lexKeywords[] = {
    {.id = string_static("if"), .token = ScriptTokenKind_If},
    {.id = string_static("else"), .token = ScriptTokenKind_Else},
    {.id = string_static("var"), .token = ScriptTokenKind_Var},
    {.id = string_static("while"), .token = ScriptTokenKind_While},
    {.id = string_static("continue"), .token = ScriptTokenKind_Continue},
    {.id = string_static("break"), .token = ScriptTokenKind_Break},
    {.id = string_static("for"), .token = ScriptTokenKind_For},
    {.id = string_static("return"), .token = ScriptTokenKind_Return},
};

static void script_lex_keywords_init(void) {
  static bool           g_init;
  static ThreadSpinLock g_initLock;
  if (g_init) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_init) {
    array_for_t(g_lexKeywords, ScriptLexKeyword, kw) { kw->idHash = string_hash(kw->id); }
    g_init = true;
  }
  thread_spinlock_unlock(&g_initLock);
}

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
  return script_consume_chars(str, math_max(script_scan_word_end(str), 1));
}

static u8 script_peek(const String str, const u32 ahead) {
  return UNLIKELY(str.size <= ahead) ? '\0' : *string_at(str, ahead);
}

static String script_lex_number_positive(String str, ScriptToken* out) {
  f64  mantissa       = 0.0;
  f64  divider        = 1.0;
  bool passedDecPoint = false;
  bool invalidChar    = false;

  u8 lastChar = '\0';
  while (!string_is_empty(str)) {
    const u8 ch = *string_begin(str);
    switch (ch) {
    case '.':
      if (UNLIKELY(passedDecPoint)) {
        lastChar = ch;
        str      = script_consume_chars(str, 1);
        goto NumberEnd;
      }
      passedDecPoint = true;
      break;
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
      mantissa = mantissa * 10.0 + (ch - '0');
      if (passedDecPoint) {
        divider *= 10.0;
      }
      break;
    case '_':
      break; // Ignore underscores as legal digit separators.
    default:
      if (script_is_word_separator(ch)) {
        goto NumberEnd;
      }
      invalidChar = true;
      break;
    }
    lastChar = ch;
    str      = script_consume_chars(str, 1);
  }

NumberEnd:
  if (UNLIKELY(invalidChar)) {
    return *out = script_token_diag(ScriptDiag_InvalidCharInNumber), str;
  }
  if (UNLIKELY(lastChar == '.')) {
    return *out = script_token_diag(ScriptDiag_NumberEndsWithDecPoint), str;
  }
  if (UNLIKELY(lastChar == '_')) {
    return *out = script_token_diag(ScriptDiag_NumberEndsWithSeparator), str;
  }
  out->kind       = ScriptTokenKind_Number;
  out->val_number = mantissa / divider;
  return str;
}

static String script_lex_key(String str, StringTable* stringtable, ScriptToken* out) {
  diag_assert(*string_begin(str) == '$');
  str = script_consume_chars(str, 1); // Skip the leading '$'.

  const u32 end = script_scan_word_end(str);
  if (UNLIKELY(!end)) {
    return *out = script_token_diag(ScriptDiag_KeyEmpty), str;
  }

  const String key = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(key))) {
    return *out = script_token_diag(ScriptDiag_InvalidUtf8), str;
  }
  const StringHash keyHash = stringtable ? stringtable_add(stringtable, key) : string_hash(key);

  out->kind    = ScriptTokenKind_Key;
  out->val_key = keyHash;
  return script_consume_chars(str, end);
}

static String script_lex_string(String str, StringTable* stringtable, ScriptToken* out) {
  diag_assert(*string_begin(str) == '"');
  str = script_consume_chars(str, 1); // Skip the leading '"'.

  const u32 end = script_scan_string_end(str);
  if (UNLIKELY(end == str.size || *string_at(str, end) != '"')) {
    return *out = script_token_diag(ScriptDiag_UnterminatedString), str;
  }

  const String val = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(val))) {
    return *out = script_token_diag(ScriptDiag_InvalidUtf8), str;
  }
  const StringHash valHash = stringtable ? stringtable_add(stringtable, val) : string_hash(val);

  out->kind       = ScriptTokenKind_String;
  out->val_string = valHash;
  return script_consume_chars(str, end + 1); // + 1 for the closing '"'.
}

static String script_lex_identifier(const String str, ScriptToken* out) {
  const u32 end = script_scan_word_end(str);
  diag_assert(end);

  const String id = string_slice(str, 0, end);
  if (UNLIKELY(!utf8_validate(id))) {
    return *out = script_token_diag(ScriptDiag_InvalidUtf8), str;
  }
  const StringHash idHash = string_hash(id);

  script_lex_keywords_init();
  array_for_t(g_lexKeywords, ScriptLexKeyword, keyword) {
    if (idHash == keyword->idHash) {
      return out->kind = keyword->token, script_consume_chars(str, end);
    }
  }

  out->kind           = ScriptTokenKind_Identifier;
  out->val_identifier = idHash;
  return script_consume_chars(str, end);
}

String script_lex(String str, StringTable* stringtable, ScriptToken* out, const ScriptLexFlags fl) {
  while (!string_is_empty(str)) {
    const u8 c = string_begin(str)[0];
    switch (c) {
    case '(':
      return out->kind = ScriptTokenKind_ParenOpen, script_consume_chars(str, 1);
    case ')':
      return out->kind = ScriptTokenKind_ParenClose, script_consume_chars(str, 1);
    case '{':
      return out->kind = ScriptTokenKind_CurlyOpen, script_consume_chars(str, 1);
    case '}':
      return out->kind = ScriptTokenKind_CurlyClose, script_consume_chars(str, 1);
    case ',':
      return out->kind = ScriptTokenKind_Comma, script_consume_chars(str, 1);
    case '=':
      if (script_peek(str, 1) == '=') {
        return out->kind = ScriptTokenKind_EqEq, script_consume_chars(str, 2);
      }
      return out->kind = ScriptTokenKind_Eq, script_consume_chars(str, 1);
    case '!':
      if (script_peek(str, 1) == '=') {
        return out->kind = ScriptTokenKind_BangEq, script_consume_chars(str, 2);
      }
      return out->kind = ScriptTokenKind_Bang, script_consume_chars(str, 1);
    case '<':
      if (script_peek(str, 1) == '=') {
        return out->kind = ScriptTokenKind_LeEq, script_consume_chars(str, 2);
      }
      return out->kind = ScriptTokenKind_Le, script_consume_chars(str, 1);
    case '>':
      if (script_peek(str, 1) == '=') {
        return out->kind = ScriptTokenKind_GtEq, script_consume_chars(str, 2);
      }
      return out->kind = ScriptTokenKind_Gt, script_consume_chars(str, 1);
    case ':':
      return out->kind = ScriptTokenKind_Colon, script_consume_chars(str, 1);
    case ';':
      return out->kind = ScriptTokenKind_Semicolon, script_consume_chars(str, 1);
    case '+':
      if (script_peek(str, 1) == '=') {
        return out->kind = ScriptTokenKind_PlusEq, script_consume_chars(str, 2);
      }
      return out->kind = ScriptTokenKind_Plus, script_consume_chars(str, 1);
    case '-':
      if (script_peek(str, 1) == '=') {
        return out->kind = ScriptTokenKind_MinusEq, script_consume_chars(str, 2);
      }
      return out->kind = ScriptTokenKind_Minus, script_consume_chars(str, 1);
    case '*':
      if (script_peek(str, 1) == '=') {
        return out->kind = ScriptTokenKind_StarEq, script_consume_chars(str, 2);
      }
      return out->kind = ScriptTokenKind_Star, script_consume_chars(str, 1);
    case '/':
      if (script_peek(str, 1) == '=') {
        return out->kind = ScriptTokenKind_SlashEq, script_consume_chars(str, 2);
      }
      if (script_peek(str, 1) == '/') {
        str = script_consume_chars(str, script_scan_line_end(str)); // Consume comment.
        if (fl & ScriptLexFlags_IncludeComments) {
          return out->kind = ScriptTokenKind_CommentLine, str;
        }
        continue;
      }
      if (script_peek(str, 1) == '*') {
        str = script_consume_chars(str, script_scan_block_comment_end(str)); // Consume comment.
        if (fl & ScriptLexFlags_IncludeComments) {
          return out->kind = ScriptTokenKind_CommentBlock, str;
        }
        continue;
      }
      return out->kind = ScriptTokenKind_Slash, script_consume_chars(str, 1);
    case '%':
      if (script_peek(str, 1) == '=') {
        return out->kind = ScriptTokenKind_PercentEq, script_consume_chars(str, 2);
      }
      return out->kind = ScriptTokenKind_Percent, script_consume_chars(str, 1);
    case '&':
      if (script_peek(str, 1) == '&') {
        return out->kind = ScriptTokenKind_AmpAmp, script_consume_chars(str, 2);
      }
      return *out = script_token_diag(ScriptDiag_InvalidChar), script_consume_chars(str, 1);
    case '|':
      if (script_peek(str, 1) == '|') {
        return out->kind = ScriptTokenKind_PipePipe, script_consume_chars(str, 2);
      }
      return *out = script_token_diag(ScriptDiag_InvalidChar), script_consume_chars(str, 1);
    case '?':
      if (script_peek(str, 1) == '?') {
        if (script_peek(str, 2) == '=') {
          return out->kind = ScriptTokenKind_QMarkQMarkEq, script_consume_chars(str, 3);
        }
        return out->kind = ScriptTokenKind_QMarkQMark, script_consume_chars(str, 2);
      }
      return out->kind = ScriptTokenKind_QMark, script_consume_chars(str, 1);
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
        return out->kind = ScriptTokenKind_Newline, script_consume_chars(str, 1);
      }
      // Fallthrough.
    case ' ':
    case '\r':
    case '\t':
      str = script_consume_chars(str, 1); // Skip whitespace.
      continue;
    default:
      if (script_is_word_start(c)) {
        return script_lex_identifier(str, out);
      }
      return *out = script_token_diag(ScriptDiag_InvalidChar), script_consume_word_or_char(str);
    }
  }

  return out->kind = ScriptTokenKind_End, string_empty;
}

String script_lex_trim(String str, const ScriptLexFlags fl) {
  while (!string_is_empty(str)) {
    const u8 c = string_begin(str)[0];
    switch (c) {
    case '/': {
      if (!(fl & ScriptLexFlags_IncludeComments)) {
        if (script_peek(str, 1) == '/') {
          str = script_consume_chars(str, script_scan_line_end(str)); // Skip comment.
          continue;
        }
        if (script_peek(str, 1) == '*') {
          str = script_consume_chars(str, script_scan_block_comment_end(str)); // Skip comment.
          continue;
        }
      }
      return str;
    }
    case '\n':
      if (fl & ScriptLexFlags_IncludeNewlines) {
        return str;
      }
      // Fallthrough.
    case ' ':
    case '\r':
    case '\t':
      str = script_consume_chars(str, 1); // Skip whitespace.
      continue;
    default:
      return str;
    }
  }
  return string_empty;
}

u32 script_lex_keyword_count(void) { return (u32)array_elems(g_lexKeywords); }

const ScriptLexKeyword* script_lex_keyword_data(void) {
  script_lex_keywords_init();
  return g_lexKeywords;
}

bool script_token_equal(const ScriptToken* a, const ScriptToken* b) {
  if (a->kind != b->kind) {
    return false;
  }
  switch (a->kind) {
  case ScriptTokenKind_Number:
    return a->val_number == b->val_number;
  case ScriptTokenKind_Identifier:
    return a->val_identifier == b->val_identifier;
  case ScriptTokenKind_Key:
    return a->val_key == b->val_key;
  case ScriptTokenKind_String:
    return a->val_string == b->val_string;
  case ScriptTokenKind_Diag:
    return a->val_diag == b->val_diag;
  default:
    return true;
  }
}

String script_token_str_scratch(const ScriptToken* token) {
  switch (token->kind) {
  case ScriptTokenKind_ParenOpen:
    return string_lit("(");
  case ScriptTokenKind_ParenClose:
    return string_lit(")");
  case ScriptTokenKind_CurlyOpen:
    return string_lit("{");
  case ScriptTokenKind_CurlyClose:
    return string_lit("}");
  case ScriptTokenKind_Comma:
    return string_lit(",");
  case ScriptTokenKind_Eq:
    return string_lit("=");
  case ScriptTokenKind_EqEq:
    return string_lit("==");
  case ScriptTokenKind_Bang:
    return string_lit("!");
  case ScriptTokenKind_BangEq:
    return string_lit("!=");
  case ScriptTokenKind_Le:
    return string_lit("<");
  case ScriptTokenKind_LeEq:
    return string_lit("<=");
  case ScriptTokenKind_Gt:
    return string_lit(">");
  case ScriptTokenKind_GtEq:
    return string_lit(">=");
  case ScriptTokenKind_Plus:
    return string_lit("+");
  case ScriptTokenKind_PlusEq:
    return string_lit("+=");
  case ScriptTokenKind_Minus:
    return string_lit("-");
  case ScriptTokenKind_MinusEq:
    return string_lit("-=");
  case ScriptTokenKind_Star:
    return string_lit("*");
  case ScriptTokenKind_StarEq:
    return string_lit("*=");
  case ScriptTokenKind_Slash:
    return string_lit("/");
  case ScriptTokenKind_SlashEq:
    return string_lit("/=");
  case ScriptTokenKind_Percent:
    return string_lit("%");
  case ScriptTokenKind_PercentEq:
    return string_lit("%=");
  case ScriptTokenKind_Colon:
    return string_lit(":");
  case ScriptTokenKind_Semicolon:
    return string_lit(";");
  case ScriptTokenKind_AmpAmp:
    return string_lit("&&");
  case ScriptTokenKind_PipePipe:
    return string_lit("||");
  case ScriptTokenKind_QMark:
    return string_lit("?");
  case ScriptTokenKind_QMarkQMark:
    return string_lit("??");
  case ScriptTokenKind_QMarkQMarkEq:
    return string_lit("?\?=");
  case ScriptTokenKind_Number:
    return fmt_write_scratch("{}", fmt_float(token->val_number));
  case ScriptTokenKind_Identifier:
    return fmt_write_scratch("{}", fmt_int(token->val_identifier, .base = 16));
  case ScriptTokenKind_Key:
    return fmt_write_scratch("${}", fmt_int(token->val_key, .base = 16));
  case ScriptTokenKind_String:
    return fmt_write_scratch("#{}", fmt_int(token->val_string, .base = 16));
  case ScriptTokenKind_If:
    return string_lit("if");
  case ScriptTokenKind_Else:
    return string_lit("else");
  case ScriptTokenKind_Var:
    return string_lit("var");
  case ScriptTokenKind_While:
    return string_lit("while");
  case ScriptTokenKind_For:
    return string_lit("for");
  case ScriptTokenKind_Continue:
    return string_lit("continue");
  case ScriptTokenKind_Break:
    return string_lit("break");
  case ScriptTokenKind_Return:
    return string_lit("return");
  case ScriptTokenKind_CommentLine:
    return string_lit("comment-line");
  case ScriptTokenKind_CommentBlock:
    return string_lit("comment-block");
  case ScriptTokenKind_Newline:
    return string_lit("newline");
  case ScriptTokenKind_Diag:
    return string_lit("diag");
  case ScriptTokenKind_End:
    return string_lit("\0");
  }
  diag_assert_fail("Unknown token-kind");
  UNREACHABLE
}
