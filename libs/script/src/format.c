#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_math.h"
#include "core_utf8.h"
#include "script_format.h"
#include "script_lex.h"

#define script_format_align_entries_max 64
#define script_format_align_diff_max 25

typedef enum {
  FormatAtomKind_Generic,
  FormatAtomKind_Newline,               // '\n'
  FormatAtomKind_BlockStart,            // '{'
  FormatAtomKind_BlockEnd,              // '}'
  FormatAtomKind_SetStart,              // '('
  FormatAtomKind_SetEnd,                // ')'
  FormatAtomKind_Identifier,            // 'hello'
  FormatAtomKind_Separator,             // ';', ','
  FormatAtomKind_Assignment,            // '='
  FormatAtomKind_CommentLine,           // '// Hello'
  FormatAtomKind_CommentBlock,          // '/* Hello */'
  FormatAtomKind_CommentBlockMultiLine, // '/* Hello \n World */'
} FormatAtomKind;

typedef struct {
  FormatAtomKind kind;
  u32            padding;
  String         text;
} FormatAtom;

typedef struct {
  u32 atomIndex, atomCount;
} FormatSpan;

typedef struct {
  const ScriptFormatSettings* settings;
  String                      input, inputTotal;
  DynString*                  out;
  DynArray*                   atoms; // FormatAtom[]
  DynArray*                   lines; // FormatSpan[]
  u32                         currentIndent;
} FormatContext;

static bool format_separate_by_space(const FormatAtom* a, const FormatAtom* b) {
  if (b->kind == FormatAtomKind_Separator) {
    return false;
  }
  if (b->kind == FormatAtomKind_SetEnd) {
    return false;
  }
  if (a->kind == FormatAtomKind_SetStart) {
    return false;
  }
  if (a->kind == FormatAtomKind_Identifier && b->kind == FormatAtomKind_SetStart) {
    return false;
  }
  return true;
}

static FormatAtomKind format_atom_kind(const ScriptTokenKind tokenKind) {
  switch (tokenKind) {
  case ScriptTokenKind_Newline:
    return FormatAtomKind_Newline;
  case ScriptTokenKind_CurlyOpen:
    return FormatAtomKind_BlockStart;
  case ScriptTokenKind_CurlyClose:
    return FormatAtomKind_BlockEnd;
  case ScriptTokenKind_ParenOpen:
    return FormatAtomKind_SetStart;
  case ScriptTokenKind_ParenClose:
    return FormatAtomKind_SetEnd;
  case ScriptTokenKind_Identifier:
    return FormatAtomKind_Identifier;
  case ScriptTokenKind_Semicolon:
  case ScriptTokenKind_Comma:
    return FormatAtomKind_Separator;
  case ScriptTokenKind_Eq:
    return FormatAtomKind_Assignment;
  case ScriptTokenKind_CommentLine:
    return FormatAtomKind_CommentLine;
  case ScriptTokenKind_CommentBlock:
    return FormatAtomKind_CommentBlock;
  default:
    return FormatAtomKind_Generic;
  }
}

static bool format_span_is_empty(const FormatSpan span) { return span.atomCount == 0; }

static FormatAtom* format_span_at(FormatContext* ctx, const FormatSpan span, const u32 i) {
  diag_assert(i < span.atomCount);
  return dynarray_begin_t(ctx->atoms, FormatAtom) + span.atomIndex + i;
}

static FormatSpan format_span_slice(const FormatSpan span, const u32 offset, const u32 size) {
  diag_assert(span.atomCount >= offset + size);
  return (FormatSpan){.atomIndex = span.atomIndex + offset, .atomCount = size};
}

static u32 format_span_measure(FormatContext* ctx, const FormatSpan span) {
  u32 result = 0;
  for (u32 i = 0; i != span.atomCount; ++i) {
    const FormatAtom* atom = format_span_at(ctx, span, i);
    result += atom->padding;
    result += (u32)utf8_cp_count(atom->text);
    if (i != (span.atomCount - 1)) {
      const FormatAtom* atomNext = format_span_at(ctx, span, i + 1);
      if (format_separate_by_space(atom, atomNext)) {
        result += 1;
      }
    }
  }
  return result;
}

static void format_span_render(FormatContext* ctx, const FormatSpan span) {
  for (u32 i = 0; i != span.atomCount; ++i) {
    const FormatAtom* atom = format_span_at(ctx, span, i);
    dynstring_append_chars(ctx->out, ' ', atom->padding);
    dynstring_append(ctx->out, atom->text);
    if (i != (span.atomCount - 1)) {
      const FormatAtom* atomNext = format_span_at(ctx, span, i + 1);
      if (format_separate_by_space(atom, atomNext)) {
        dynstring_append_char(ctx->out, ' ');
      }
    }
  }
}

static bool token_is_unary(const ScriptTokenKind tokenKind) {
  switch (tokenKind) {
  case ScriptTokenKind_Bang:
  case ScriptTokenKind_Minus:
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
  if (UNLIKELY(tok.kind == ScriptTokenKind_End)) {
    return false;
  }

  /**
   * Merge unary operators into the next token if they are not separated in the input.
   *
   * Reason is that unary and binary operators have different separation rules (binary are separated
   * by spaces while unary are not), but for tokens that can both be used as unary or binary
   * operators (like the minus sign) we cannot tell which to use without implementing a full parser.
   */
  while (token_is_unary(tok.kind) && ctx->input.size == script_lex_trim(ctx->input, flags).size) {
    ctx->input = script_lex(ctx->input, null, &tok, flags);
  }

  const usize  offsetEnd     = ctx->inputTotal.size - ctx->input.size;
  const String textUntrimmed = string_slice(ctx->inputTotal, offsetStart, offsetEnd - offsetStart);
  const String text          = script_lex_trim(textUntrimmed, flags);

  FormatAtomKind kind = format_atom_kind(tok.kind);
  if (kind == FormatAtomKind_CommentBlock && mem_contains(text, '\n')) {
    kind = FormatAtomKind_CommentBlockMultiLine;
  }

  *out = (FormatAtom){.kind = kind, .text = text};
  return true;
}

static bool format_span_read_line(FormatContext* ctx, FormatSpan* out) {
  out->atomIndex = (u32)ctx->atoms->size;

  FormatAtom atom;
  while (format_read_atom(ctx, &atom)) {
    if (atom.kind == FormatAtomKind_Newline) {
      out->atomCount = (u32)ctx->atoms->size - out->atomIndex;
      return true;
    }
    if (atom.kind == FormatAtomKind_BlockEnd || atom.kind == FormatAtomKind_SetEnd) {
      if (ctx->currentIndent) {
        --ctx->currentIndent;
      }
    }
    const bool firstAtom = out->atomIndex == ctx->atoms->size;
    if (firstAtom) {
      atom.padding = ctx->currentIndent * ctx->settings->indentSize;
    }
    if (atom.kind == FormatAtomKind_BlockStart || atom.kind == FormatAtomKind_SetStart) {
      ++ctx->currentIndent;
    }
    *dynarray_push_t(ctx->atoms, FormatAtom) = atom; // Output the atom.
  }

  out->atomCount = (u32)ctx->atoms->size - out->atomIndex;
  return !format_span_is_empty(*out);
}

static void format_span_read_all_lines(FormatContext* ctx) {
  bool       lastLineEmpty = false;
  FormatSpan line;
  while (format_span_read_line(ctx, &line)) {
    const bool lineEmpty = format_span_is_empty(line);
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

typedef struct {
  u32 distance;
  u32 atomIndex;
} FormatAlignEntry;

static void format_align_apply(
    FormatContext*         ctx,
    const u32              distance,
    const FormatAlignEntry entries[PARAM_ARRAY_SIZE(script_format_align_entries_max)],
    const u32              entryCount) {
  for (u32 i = 0; i != entryCount; ++i) {
    FormatAtom* atom = dynarray_at_t(ctx->atoms, entries[i].atomIndex, FormatAtom);
    if (distance > entries[i].distance) {
      atom->padding = distance - entries[i].distance;
    }
  }
}

static u32 format_align_target(FormatContext* ctx, const FormatSpan span, const FormatAtomKind t) {
  for (u32 i = 0; i != span.atomCount; ++i) {
    const FormatAtom* atom = format_span_at(ctx, span, i);
    // NOTE: Skip the first atom as it doesn't need / support aligning.
    if (i != 0 && atom->kind == t) {
      return i;
    }
    switch (atom->kind) {
    case FormatAtomKind_BlockStart:
    case FormatAtomKind_BlockEnd:
    case FormatAtomKind_SetStart:
    case FormatAtomKind_SetEnd:
    case FormatAtomKind_CommentBlockMultiLine:
      return sentinel_u32; // Alignment boundary encountered.
    default:
      break;
    }
  }
  return sentinel_u32; // Target not found.
}

static void format_align_all(FormatContext* ctx, const FormatAtomKind kind) {
  FormatAlignEntry entries[script_format_align_entries_max];
  u32              entryCount    = 0;
  u32              alignDistance = 0;
  for (u32 i = 0; i != ctx->lines->size; ++i) {
    const FormatSpan* line        = dynarray_at_t(ctx->lines, i, FormatSpan);
    const u32         targetIndex = format_align_target(ctx, *line, kind);
    if (sentinel_check(targetIndex)) {
      format_align_apply(ctx, alignDistance, entries, entryCount);
      entryCount = alignDistance = 0;
      continue;
    }
    if (UNLIKELY(entryCount == script_format_align_entries_max)) {
      format_align_apply(ctx, alignDistance, entries, entryCount);
      entryCount = alignDistance = 0;
    }
    const u32 distance = format_span_measure(ctx, format_span_slice(*line, 0, targetIndex));
    if (UNLIKELY(math_abs((i32)distance - (i32)alignDistance) > script_format_align_diff_max)) {
      format_align_apply(ctx, alignDistance, entries, entryCount);
      entryCount = alignDistance = 0;
    }
    entries[entryCount++] = (FormatAlignEntry){
        .distance  = distance,
        .atomIndex = line->atomIndex + targetIndex,
    };
    alignDistance = math_max(alignDistance, distance);
  }
  format_align_apply(ctx, alignDistance, entries, entryCount);
}

void script_format(DynString* out, const String input, const ScriptFormatSettings* settings) {
  DynArray      atoms = dynarray_create_t(g_allocHeap, FormatAtom, 4096);
  DynArray      lines = dynarray_create_t(g_allocHeap, FormatSpan, 512);
  FormatContext ctx   = {
        .settings   = settings,
        .input      = input,
        .inputTotal = input,
        .out        = out,
        .atoms      = &atoms,
        .lines      = &lines,
  };

  format_span_read_all_lines(&ctx);
  if (lines.size) {
    format_align_all(&ctx, FormatAtomKind_Assignment);
    format_align_all(&ctx, FormatAtomKind_CommentLine);

    dynarray_for_t(&lines, FormatSpan, line) {
      format_span_render(&ctx, *line);
      dynstring_append_char(ctx.out, '\n');
    }
  } else {
    dynstring_append_char(out, '\n');
  }

  dynarray_destroy(&atoms);
  dynarray_destroy(&lines);
}
