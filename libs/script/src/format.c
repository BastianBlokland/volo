#include "core_alloc.h"
#include "script_format.h"
#include "script_lex.h"

#define script_format_indent_size 2

typedef enum {
  FormatAtomType_Generic,
  FormatAtomType_Newline,       // '\n'
  FormatAtomType_BlockStart,    // '{'
  FormatAtomType_BlockEnd,      // '}'
  FormatAtomType_SetStart,      // '('
  FormatAtomType_SetEnd,        // ')'
  FormatAtomType_Identifier,    // 'hello'
  FormatAtomType_Separator,     // ';', ','
  FormatAtomType_UnaryOperator, // '!'
} FormatAtomType;

typedef struct {
  FormatAtomType type;
  String         text;
} FormatAtom;

typedef struct {
  usize atomStart, atomEnd;
  u32   indent;
} FormatLine;

typedef struct {
  String     input, inputTotal;
  DynString* out;
  DynArray*  atoms; // FormatAtom[]
  DynArray*  lines; // FormatLine[]
  u32        currentIndent;
} FormatContext;

static bool format_read_atom(FormatContext* ctx, FormatAtom* out) {
  const ScriptLexFlags lexFlags = ScriptLexFlags_IncludeNewlines | ScriptLexFlags_IncludeComments;

  const usize    offsetStart = ctx->inputTotal.size - ctx->input.size;
  FormatAtomType type        = FormatAtomType_Generic;

  ScriptToken token;
  ctx->input = script_lex(ctx->input, null, &token, lexFlags);
  switch (token.type) {
  case ScriptTokenType_End:
    return false;
  case ScriptTokenType_Newline:
    type = FormatAtomType_Newline;
    break;
  case ScriptTokenType_CurlyOpen:
    type = FormatAtomType_BlockStart;
    break;
  case ScriptTokenType_CurlyClose:
    type = FormatAtomType_BlockEnd;
    break;
  case ScriptTokenType_ParenOpen:
    type = FormatAtomType_SetStart;
    break;
  case ScriptTokenType_ParenClose:
    type = FormatAtomType_SetEnd;
    break;
  case ScriptTokenType_Identifier:
    type = FormatAtomType_Identifier;
    break;
  case ScriptTokenType_Semicolon:
  case ScriptTokenType_Comma:
    type = FormatAtomType_Separator;
    break;
  case ScriptTokenType_Bang:
    type = FormatAtomType_UnaryOperator;
    break;
  default:
    break;
  }

  const usize  offsetEnd     = ctx->inputTotal.size - ctx->input.size;
  const String textUntrimmed = string_slice(ctx->inputTotal, offsetStart, offsetEnd - offsetStart);
  const String text          = script_lex_trim(textUntrimmed, lexFlags);

  *out = (FormatAtom){.type = type, .text = text};
  return true;
}

static bool format_read_line(FormatContext* ctx, FormatLine* out) {
  out->indent    = ctx->currentIndent;
  out->atomStart = ctx->atoms->size;

  FormatAtom atom;
  while (format_read_atom(ctx, &atom)) {
    switch (atom.type) {
    case FormatAtomType_Newline:
      out->atomEnd = ctx->atoms->size;
      return true;
    case FormatAtomType_BlockStart:
      ++ctx->currentIndent;
      break;
    case FormatAtomType_BlockEnd:
      if (out->atomStart == ctx->atoms->size) {
        --out->indent; // Line starts with closing-curly; reduce indent.
      }
      if (ctx->currentIndent) {
        --ctx->currentIndent;
      }
      break;
    default:
      break;
    }
    *dynarray_push_t(ctx->atoms, FormatAtom) = atom; // Output the atom.
  }
  return false; // No atoms left.
}

static void format_read_all_lines(FormatContext* ctx) {
  FormatLine line;
  while (format_read_line(ctx, &line)) {
    *dynarray_push_t(ctx->lines, FormatLine) = line;
  }
}

static bool format_read_use_separator(const FormatAtom* a, const FormatAtom* b) {
  switch (b->type) {
  case FormatAtomType_SetStart:
    if (a->type == FormatAtomType_Identifier) {
      return false;
    }
    return true;
  case FormatAtomType_SetEnd:
  case FormatAtomType_Separator:
    return false;
  default:
    switch (a->type) {
    case FormatAtomType_SetStart:
    case FormatAtomType_UnaryOperator:
      return false;
    default:
      return true;
    }
  }
  return true;
}

static void format_render_line(FormatContext* ctx, const FormatLine* line) {
  if (line->atomStart != line->atomEnd) {
    dynstring_append_chars(ctx->out, ' ', line->indent * script_format_indent_size);
  }
  for (usize atomIdx = line->atomStart; atomIdx != line->atomEnd; ++atomIdx) {
    const FormatAtom* atom = dynarray_at_t(ctx->atoms, atomIdx, FormatAtom);
    dynstring_append(ctx->out, atom->text);

    const bool lastAtom = atomIdx == (line->atomEnd - 1);
    if (!lastAtom) {
      const FormatAtom* atomNext = dynarray_at_t(ctx->atoms, atomIdx + 1, FormatAtom);
      if (format_read_use_separator(atom, atomNext)) {
        dynstring_append_char(ctx->out, ' ');
      }
    }
  }
  dynstring_append_char(ctx->out, '\n');
}

static void format_render_all_lines(FormatContext* ctx) {
  dynarray_for_t(ctx->lines, FormatLine, line) { format_render_line(ctx, line); }
}

void script_format(DynString* out, const String input) {
  DynArray      atoms = dynarray_create_t(g_alloc_heap, FormatAtom, 4096);
  DynArray      lines = dynarray_create_t(g_alloc_heap, FormatLine, 512);
  FormatContext ctx   = {
      .input      = input,
      .inputTotal = input,
      .out        = out,
      .atoms      = &atoms,
      .lines      = &lines,
  };

  format_read_all_lines(&ctx);
  format_render_all_lines(&ctx);

  dynarray_destroy(&atoms);
  dynarray_destroy(&lines);
}
