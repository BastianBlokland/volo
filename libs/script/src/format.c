#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_utf8.h"
#include "script_format.h"
#include "script_lex.h"

#define script_format_indent_size 2
#define script_format_align_entries_max 64

typedef enum {
  FormatAtomType_Generic,
  FormatAtomType_Newline,    // '\n'
  FormatAtomType_BlockStart, // '{'
  FormatAtomType_BlockEnd,   // '}'
  FormatAtomType_SetStart,   // '('
  FormatAtomType_SetEnd,     // ')'
  FormatAtomType_Identifier, // 'hello'
  FormatAtomType_Separator,  // ';', ','
  FormatAtomType_Assignment, // '='
} FormatAtomType;

typedef struct {
  FormatAtomType type;
  u32            padding;
  String         text;
} FormatAtom;

typedef struct {
  usize atomIndex, atomCount;
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
  case ScriptTokenType_Eq:
    return FormatAtomType_Assignment;
  default:
    return FormatAtomType_Generic;
  }
}

static bool format_span_is_empty(const FormatSpan span) { return span.atomCount == 0; }

static FormatAtom* format_span_at(FormatContext* ctx, const FormatSpan span, const usize i) {
  diag_assert(i < span.atomCount);
  return dynarray_at_t(ctx->atoms, span.atomIndex + i, FormatAtom);
}

static FormatSpan format_span_slice(const FormatSpan span, const usize offset, const usize size) {
  diag_assert(span.atomCount >= offset + size);
  return (FormatSpan){.atomIndex = span.atomIndex + offset, .atomCount = size};
}

static usize format_span_measure(FormatContext* ctx, const FormatSpan span) {
  usize result = 0;
  for (usize i = 0; i != span.atomCount; ++i) {
    const FormatAtom* atom = format_span_at(ctx, span, i);
    result += atom->padding;
    result += utf8_cp_count(atom->text);
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
  for (usize i = 0; i != span.atomCount; ++i) {
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

static bool format_span_read_line(FormatContext* ctx, FormatSpan* out) {
  out->atomIndex = ctx->atoms->size;

  FormatAtom atom;
  while (format_read_atom(ctx, &atom)) {
    if (atom.type == FormatAtomType_Newline) {
      out->atomCount = ctx->atoms->size - out->atomIndex;
      return true;
    }
    if (atom.type == FormatAtomType_BlockEnd) {
      if (ctx->currentIndent) {
        --ctx->currentIndent;
      }
    }
    const bool firstAtom = out->atomIndex == ctx->atoms->size;
    if (firstAtom) {
      atom.padding = ctx->currentIndent * script_format_indent_size;
    }
    if (atom.type == FormatAtomType_BlockStart) {
      ++ctx->currentIndent;
    }
    *dynarray_push_t(ctx->atoms, FormatAtom) = atom; // Output the atom.
  }

  out->atomCount = ctx->atoms->size - out->atomIndex;
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
  usize distance;
  usize atomIndex;
} FormatAlignEntry;

static void format_align_apply(
    FormatContext*         ctx,
    const usize            distance,
    const FormatAlignEntry entries[PARAM_ARRAY_SIZE(script_format_align_entries_max)],
    const u32              entryCount) {
  for (u32 i = 0; i != entryCount; ++i) {
    FormatAtom* atom = dynarray_at_t(ctx->atoms, entries[i].atomIndex, FormatAtom);
    if (distance > entries[i].distance) {
      atom->padding = (u32)(distance - entries[i].distance);
    }
  }
}

static usize format_align_target(FormatContext* ctx, const FormatSpan s, const FormatAtomType t) {
  for (usize i = 0; i != s.atomCount; ++i) {
    const FormatAtom* atom = format_span_at(ctx, s, i);
    if (atom->type == t) {
      return i;
    }
    switch (atom->type) {
    case FormatAtomType_BlockStart:
    case FormatAtomType_BlockEnd:
    case FormatAtomType_SetStart:
    case FormatAtomType_SetEnd:
      return sentinel_usize; // Alignment boundary encountered.
    default:
      break;
    }
  }
  return sentinel_usize; // Target not found/
}

static void format_align_all(FormatContext* ctx, const FormatAtomType type) {
  FormatAlignEntry entries[script_format_align_entries_max];
  u32              entryCount    = 0;
  usize            alignDistance = 0;
  for (usize i = 0; i != ctx->lines->size; ++i) {
    const FormatSpan* line        = dynarray_at_t(ctx->lines, i, FormatSpan);
    const usize       targetIndex = format_align_target(ctx, *line, type);
    if (sentinel_check(targetIndex)) {
      format_align_apply(ctx, alignDistance, entries, entryCount);
      entryCount = alignDistance = 0;
      continue;
    }
    if (UNLIKELY(entryCount == script_format_align_entries_max)) {
      format_align_apply(ctx, alignDistance, entries, entryCount);
      entryCount = alignDistance = 0;
    }
    const usize distance  = format_span_measure(ctx, format_span_slice(*line, 0, targetIndex));
    entries[entryCount++] = (FormatAlignEntry){
        .distance  = distance,
        .atomIndex = line->atomIndex + targetIndex,
    };
    alignDistance = math_max(alignDistance, distance);
  }
  format_align_apply(ctx, alignDistance, entries, entryCount);
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

  format_span_read_all_lines(&ctx);
  if (lines.size) {
    format_align_all(&ctx, FormatAtomType_Assignment);

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
