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
  String         text;
} FormatAtom;

typedef struct {
  usize atomStart, atomEnd;
  u32   indent;
} FormatChunk;

typedef struct {
  String     input, inputTotal;
  DynString* out;
  DynArray*  atoms;  // FormatAtom[]
  DynArray*  chunks; // FormatChunk[]
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

static bool format_read_chunk(FormatContext* ctx, FormatChunk* out) {
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
        --out->indent; // Chunk starts with closing-curly; reduce indent.
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

static void format_read_all_chunks(FormatContext* ctx) {
  FormatChunk chunk;
  while (format_read_chunk(ctx, &chunk)) {
    *dynarray_push_t(ctx->chunks, FormatChunk) = chunk;
  }
}

static void format_render_chunk(FormatContext* ctx, const FormatChunk* chunk) {
  if (chunk->atomStart != chunk->atomEnd) {
    dynstring_append_chars(ctx->out, ' ', chunk->indent * script_format_indent_size);
  }
  for (usize atomIdx = chunk->atomStart; atomIdx != chunk->atomEnd; ++atomIdx) {
    const FormatAtom* atom = dynarray_at_t(ctx->atoms, atomIdx, FormatAtom);
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

static void format_render_all_chunks(FormatContext* ctx) {
  dynarray_for_t(ctx->chunks, FormatChunk, chunk) { format_render_chunk(ctx, chunk); }
}

void script_format(DynString* out, const String input) {
  DynArray      atoms  = dynarray_create_t(g_alloc_heap, FormatAtom, 4096);
  DynArray      chunks = dynarray_create_t(g_alloc_heap, FormatChunk, 512);
  FormatContext ctx    = {
         .input      = input,
         .inputTotal = input,
         .out        = out,
         .atoms      = &atoms,
         .chunks     = &chunks,
  };

  format_read_all_chunks(&ctx);
  format_render_all_chunks(&ctx);

  dynarray_destroy(&atoms);
  dynarray_destroy(&chunks);
}
