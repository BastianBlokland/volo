#include "core_alloc.h"
#include "core_utf8.h"
#include "script_format.h"
#include "script_lex.h"

#define script_format_indent_size 2

typedef enum {
  FormatAtomType_Generic,
  FormatAtomType_Newline,    // '\n'
  FormatAtomType_BlockStart, // '{'
  FormatAtomType_BlockEnd,   // '}'
  FormatAtomType_SetStart,   // '('
  FormatAtomType_SetEnd,     // ')'
  FormatAtomType_Identifier, // 'hello'
  FormatAtomType_Separator,  // ';', ','
} FormatAtomType;

typedef struct {
  FormatAtomType type;
  u32            padding;
  String         text;
} FormatAtom;

typedef struct {
  usize atomStart, atomEnd;
} FormatSpan;

typedef struct {
  String     input, inputTotal;
  DynString* out;
  DynArray*  atoms; // FormatAtom[]
  DynArray*  lines; // FormatSpan[]
  u32        currentIndent;
} FormatContext;

static bool format_separate_by_space(const FormatAtom* a, const FormatAtom* b) {
  if (b->type == FormatAtomType_Separator) {
    return false;
  }
  if (b->type == FormatAtomType_SetEnd) {
    return false;
  }
  if (a->type == FormatAtomType_SetStart) {
    return false;
  }
  if (a->type == FormatAtomType_Identifier && b->type == FormatAtomType_SetStart) {
    return false;
  }
  return true;
}

static FormatAtomType format_atom_type(const ScriptTokenType tokenType) {
  switch (tokenType) {
  case ScriptTokenType_Newline:
    return FormatAtomType_Newline;
  case ScriptTokenType_CurlyOpen:
    return FormatAtomType_BlockStart;
  case ScriptTokenType_CurlyClose:
    return FormatAtomType_BlockEnd;
  case ScriptTokenType_ParenOpen:
    return FormatAtomType_SetStart;
  case ScriptTokenType_ParenClose:
    return FormatAtomType_SetEnd;
  case ScriptTokenType_Identifier:
    return FormatAtomType_Identifier;
  case ScriptTokenType_Semicolon:
  case ScriptTokenType_Comma:
    return FormatAtomType_Separator;
  default:
    return FormatAtomType_Generic;
  }
}

static bool format_span_is_empty(const FormatSpan* span) {
  return span->atomStart == span->atomEnd;
}

static usize format_span_measure(FormatContext* ctx, const FormatSpan* span) {
  usize result = 0;
  for (usize atomIdx = span->atomStart; atomIdx != span->atomEnd; ++atomIdx) {
    const FormatAtom* atom = dynarray_at_t(ctx->atoms, atomIdx, FormatAtom);
    result += atom->padding;
    result += utf8_cp_count(atom->text);
    if (atomIdx != (span->atomEnd - 1)) {
      const FormatAtom* atomNext = dynarray_at_t(ctx->atoms, atomIdx + 1, FormatAtom);
      if (format_separate_by_space(atom, atomNext)) {
        result += 1;
      }
    }
  }
  return result;
}

static void format_span_render(FormatContext* ctx, const FormatSpan* span) {
  for (usize atomIdx = span->atomStart; atomIdx != span->atomEnd; ++atomIdx) {
    const FormatAtom* atom = dynarray_at_t(ctx->atoms, atomIdx, FormatAtom);
    dynstring_append_chars(ctx->out, ' ', atom->padding);
    dynstring_append(ctx->out, atom->text);
    if (atomIdx != (span->atomEnd - 1)) {
      const FormatAtom* atomNext = dynarray_at_t(ctx->atoms, atomIdx + 1, FormatAtom);
      if (format_separate_by_space(atom, atomNext)) {
        dynstring_append_char(ctx->out, ' ');
      }
    }
  }
}

static bool token_is_unary(const ScriptTokenType tokenType) {
  switch (tokenType) {
  case ScriptTokenType_Bang:
  case ScriptTokenType_Minus:
    return true;
  default:
    return false;
  }
}

static bool format_read_atom(FormatContext* ctx, FormatAtom* out) {
  const ScriptLexFlags flags = ScriptLexFlags_IncludeNewlines | ScriptLexFlags_IncludeComments;

  const usize offsetStart = ctx->inputTotal.size - ctx->input.size;

  ScriptToken tok;
  ctx->input = script_lex(ctx->input, null, &tok, flags);
  if (UNLIKELY(tok.type == ScriptTokenType_End)) {
    return false;
  }

  /**
   * Merge unary operators into the next token if they are not separated in the input.
   *
   * Reason is that unary and binary operators have different separation rules (binary are separated
   * by spaces while unary are not), but for tokens that can both be used as unary or binary
   * operators (like the minus sign) we cannot tell which to use without implementing a full parser.
   */
  while (token_is_unary(tok.type) && ctx->input.size == script_lex_trim(ctx->input, flags).size) {
    ctx->input = script_lex(ctx->input, null, &tok, flags);
  }

  const usize  offsetEnd     = ctx->inputTotal.size - ctx->input.size;
  const String textUntrimmed = string_slice(ctx->inputTotal, offsetStart, offsetEnd - offsetStart);
  const String text          = script_lex_trim(textUntrimmed, flags);

  *out = (FormatAtom){.type = format_atom_type(tok.type), .text = text};
  return true;
}

static bool format_read_line(FormatContext* ctx, FormatSpan* out) {
  out->atomStart = ctx->atoms->size;

  FormatAtom atom;
  while (format_read_atom(ctx, &atom)) {
    if (atom.type == FormatAtomType_Newline) {
      out->atomEnd = ctx->atoms->size;
      return true;
    }
    if (atom.type == FormatAtomType_BlockEnd) {
      if (ctx->currentIndent) {
        --ctx->currentIndent;
      }
    }
    const bool firstAtom = out->atomStart == ctx->atoms->size;
    if (firstAtom) {
      atom.padding = ctx->currentIndent * script_format_indent_size;
    }
    if (atom.type == FormatAtomType_BlockStart) {
      ++ctx->currentIndent;
    }
    *dynarray_push_t(ctx->atoms, FormatAtom) = atom; // Output the atom.
  }

  out->atomEnd = ctx->atoms->size;
  return !format_span_is_empty(out);
}

static void format_read_all_lines(FormatContext* ctx) {
  bool       lastLineEmpty = false;
  FormatSpan line;
  while (format_read_line(ctx, &line)) {
    const bool lineEmpty = format_span_is_empty(&line);
    // Skip consecutive empty lines.
    if (!lineEmpty || !lastLineEmpty) {
      *dynarray_push_t(ctx->lines, FormatSpan) = line;
    }
    lastLineEmpty = lineEmpty;
  }
  if (lastLineEmpty) {
    dynarray_remove(ctx->lines, ctx->lines->size - 1, 1);
  }
}

void script_format(DynString* out, const String input) {
  DynArray      atoms = dynarray_create_t(g_alloc_heap, FormatAtom, 4096);
  DynArray      lines = dynarray_create_t(g_alloc_heap, FormatSpan, 512);
  FormatContext ctx   = {
      .input      = input,
      .inputTotal = input,
      .out        = out,
      .atoms      = &atoms,
      .lines      = &lines,
  };

  format_read_all_lines(&ctx);
  if (lines.size) {
    dynarray_for_t(&lines, FormatSpan, line) {
      format_span_render(&ctx, line);
      dynstring_append_char(ctx.out, '\n');
    }
  } else {
    dynstring_append_char(out, '\n');
  }

  dynarray_destroy(&atoms);
  dynarray_destroy(&lines);
}
