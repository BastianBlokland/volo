#include "core_alloc.h"
#include "script_format.h"
#include "script_lex.h"

#define script_format_indent_size 2

typedef struct {
  ScriptTokenType type;
  String          text;
} FormatToken;

typedef struct {
  usize tokenStart, tokenEnd;
  u32   indent;
} FormatLine;

typedef struct {
  String     input, inputTotal;
  DynString* out;
  DynArray*  tokens; // FormatToken[]
  DynArray*  lines;  // FormatLine[]
  u32        currentIndent;
} FormatContext;

static bool format_read_token(FormatContext* ctx, FormatToken* out) {
  const ScriptLexFlags lexFlags = ScriptLexFlags_IncludeNewlines | ScriptLexFlags_IncludeComments;

  const usize offsetStart = ctx->inputTotal.size - ctx->input.size;
  ScriptToken token;
  ctx->input = script_lex(ctx->input, null, &token, lexFlags);
  if (UNLIKELY(token.type == ScriptTokenType_End)) {
    return false;
  }
  const usize  offsetEnd     = ctx->inputTotal.size - ctx->input.size;
  const String textUntrimmed = string_slice(ctx->inputTotal, offsetStart, offsetEnd - offsetStart);
  const String text          = script_lex_trim(textUntrimmed, lexFlags);

  *out = (FormatToken){.type = token.type, .text = text};
  return true;
}

static bool format_read_line(FormatContext* ctx, FormatLine* out) {
  out->indent     = ctx->currentIndent;
  out->tokenStart = ctx->tokens->size;

  FormatToken token;
  while (format_read_token(ctx, &token)) {
    switch (token.type) {
    case ScriptTokenType_Newline:
      out->tokenEnd = ctx->tokens->size;
      return true;
    case ScriptTokenType_CurlyOpen:
      ++ctx->currentIndent;
      break;
    case ScriptTokenType_CurlyClose:
      if (out->tokenStart == ctx->tokens->size) {
        --out->indent; // Line starts with closing-curly; reduce indent.
      }
      if (ctx->currentIndent) {
        --ctx->currentIndent;
      }
      break;
    default:
      break;
    }
    *dynarray_push_t(ctx->tokens, FormatToken) = token; // Output the token.
  }
  return false; // No tokens left.
}

static void format_read_all_lines(FormatContext* ctx) {
  FormatLine line;
  while (format_read_line(ctx, &line)) {
    *dynarray_push_t(ctx->lines, FormatLine) = line;
  }
}

static void format_render_line(FormatContext* ctx, const FormatLine* line) {
  dynstring_append_chars(ctx->out, ' ', line->indent * script_format_indent_size);
  for (usize tokenIdx = line->tokenStart; tokenIdx != line->tokenEnd; ++tokenIdx) {
    const FormatToken* token = dynarray_at_t(ctx->tokens, tokenIdx, FormatToken);
    dynstring_append(ctx->out, token->text);
    if (tokenIdx < (line->tokenEnd - 1)) {
      dynstring_append_char(ctx->out, ' ');
    }
  }
  dynstring_append_char(ctx->out, '\n');
}

static void format_render_all_lines(FormatContext* ctx) {
  dynarray_for_t(ctx->lines, FormatLine, line) { format_render_line(ctx, line); }
}

void script_format(DynString* out, const String input) {
  DynArray      tokens = dynarray_create_t(g_alloc_heap, FormatToken, 4096);
  DynArray      lines  = dynarray_create_t(g_alloc_heap, FormatLine, 512);
  FormatContext ctx    = {
         .input      = input,
         .inputTotal = input,
         .out        = out,
         .tokens     = &tokens,
         .lines      = &lines,
  };

  format_read_all_lines(&ctx);
  format_render_all_lines(&ctx);

  dynarray_destroy(&tokens);
  dynarray_destroy(&lines);
}
