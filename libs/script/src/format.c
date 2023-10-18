#include "core_alloc.h"
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
} FormatChunk;

typedef struct {
  String     input, inputTotal;
  DynString* out;
  DynArray*  atoms; // FormatAtom[]
  DynArray*  lines; // FormatChunk[]
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

static bool format_chunk_is_empty(const FormatChunk* chunk) {
  return chunk->atomStart == chunk->atomEnd;
}

static bool format_is_unary(const ScriptTokenType tokenType) {
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
  while (format_is_unary(tok.type) && ctx->input.size == script_lex_trim(ctx->input, flags).size) {
    ctx->input = script_lex(ctx->input, null, &tok, flags);
  }

  const usize  offsetEnd     = ctx->inputTotal.size - ctx->input.size;
  const String textUntrimmed = string_slice(ctx->inputTotal, offsetStart, offsetEnd - offsetStart);
  const String text          = script_lex_trim(textUntrimmed, flags);

  *out = (FormatAtom){.type = format_atom_type(tok.type), .text = text};
  return true;
}

static bool format_read_line(FormatContext* ctx, FormatChunk* out) {
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
  return !format_chunk_is_empty(out);
}

static void format_read_all_lines(FormatContext* ctx) {
  bool        lastChunkEmpty = false;
  FormatChunk chunk;
  while (format_read_line(ctx, &chunk)) {
    const bool chunkEmpty = format_chunk_is_empty(&chunk);
    // Skip consecutive empty lines.
    if (!chunkEmpty || !lastChunkEmpty) {
      *dynarray_push_t(ctx->lines, FormatChunk) = chunk;
    }
    lastChunkEmpty = chunkEmpty;
  }
  if (lastChunkEmpty) {
    dynarray_remove(ctx->lines, ctx->lines->size - 1, 1);
  }
}

static void format_render_line(FormatContext* ctx, const FormatChunk* chunk) {
  for (usize atomIdx = chunk->atomStart; atomIdx != chunk->atomEnd; ++atomIdx) {
    const FormatAtom* atom = dynarray_at_t(ctx->atoms, atomIdx, FormatAtom);
    dynstring_append_chars(ctx->out, ' ', atom->padding);
    dynstring_append(ctx->out, atom->text);

    const bool lastAtom = atomIdx == (chunk->atomEnd - 1);
    if (!lastAtom) {
      const FormatAtom* atomNext = dynarray_at_t(ctx->atoms, atomIdx + 1, FormatAtom);
      if (format_separate_by_space(atom, atomNext)) {
        dynstring_append_char(ctx->out, ' ');
      }
    }
  }
  dynstring_append_char(ctx->out, '\n');
}

void script_format(DynString* out, const String input) {
  DynArray      atoms = dynarray_create_t(g_alloc_heap, FormatAtom, 4096);
  DynArray      lines = dynarray_create_t(g_alloc_heap, FormatChunk, 512);
  FormatContext ctx   = {
      .input      = input,
      .inputTotal = input,
      .out        = out,
      .atoms      = &atoms,
      .lines      = &lines,
  };

  format_read_all_lines(&ctx);
  if (lines.size) {
    dynarray_for_t(&lines, FormatChunk, chunk) { format_render_line(&ctx, chunk); }
  } else {
    dynstring_append_char(out, '\n');
  }

  dynarray_destroy(&atoms);
  dynarray_destroy(&lines);
}
