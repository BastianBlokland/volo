#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_search.h"
#include "core_utf8.h"
#include "script_lex.h"
#include "script_pos.h"

ScriptPosLineCol script_pos_to_line_col(const String src, const ScriptPos pos) {
  diag_assert(pos <= src.size);
  u32 currentPos = 0;
  u16 line = 0, column = 0;
  while (currentPos < pos) {
    const u8 ch = *string_at(src, currentPos);
    switch (ch) {
    case '\n':
      ++currentPos;
      ++line;
      column = 0;
      break;
    case '\r':
      ++currentPos;
      break;
    default:
      currentPos += (u32)math_max(utf8_cp_bytes_from_first(ch), 1);
      ++column;
    }
  }
  return (ScriptPosLineCol){.line = line, .column = column};
}

ScriptPos script_pos_from_line_col(const String src, const ScriptPosLineCol lc) {
  u32 currentPos = 0;

  // Advance 'lc.line' lines.
  for (u16 line = 0; line != lc.line; ++line) {
    // Advance until the end of the line.
    for (;;) {
      if (UNLIKELY(currentPos == src.size)) {
        return script_pos_sentinel;
      }
      const u8 ch = *string_at(src, currentPos);
      ++currentPos;
      if (ch == '\n') {
        break;
      }
    }
  }

  // Advance 'lc.column' columns.
  for (u16 col = 0; col != lc.column; ++col) {
    if (UNLIKELY(currentPos >= src.size)) {
      return script_pos_sentinel;
    }
    const u8 ch = *string_at(src, currentPos);
    currentPos += (u32)math_max(utf8_cp_bytes_from_first(ch), 1);
  }

  return currentPos;
}

ScriptRange script_range(const ScriptPos start, const ScriptPos end) {
  diag_assert(end >= start);
  return (ScriptRange){.start = start, .end = end};
}

bool script_range_valid(const ScriptRange range) {
  return !sentinel_check(range.start) && !sentinel_check(range.end);
}

bool script_range_contains(const ScriptRange range, const ScriptPos pos) {
  return pos >= range.start && pos < range.end;
}

bool script_range_subrange(const ScriptRange a, const ScriptRange b) {
  return a.start <= b.start && a.end >= b.end;
}

ScriptRange script_range_full(String src) { return script_range(0, (u32)src.size); }

String script_range_text(const String src, const ScriptRange range) {
  diag_assert(range.end >= range.start);
  return string_slice(src, range.start, range.end - range.start);
}

ScriptPos script_pos_trim(const String src, const ScriptPos pos) {
  const String toEnd        = string_consume(src, pos);
  const String toEndTrimmed = script_lex_trim(toEnd, ScriptLexFlags_None);
  return (ScriptPos)(src.size - toEndTrimmed.size);
}

ScriptRangeLineCol script_range_to_line_col(const String src, const ScriptRange range) {
  return (ScriptRangeLineCol){
      .start = script_pos_to_line_col(src, range.start),
      .end   = script_pos_to_line_col(src, range.end),
  };
}

ScriptRange script_range_from_line_col(const String src, const ScriptRangeLineCol range) {
  return (ScriptRange){
      .start = script_pos_from_line_col(src, range.start),
      .end   = script_pos_from_line_col(src, range.end),
  };
}

struct sScriptLookup {
  Allocator* alloc;
  usize      srcSize;
  String     srcBuffer;
  DynArray   lineEnds; // ScriptPos[], sorted positions in the source where a line ends.
};

ScriptLookup* script_lookup_create(Allocator* alloc) {
  ScriptLookup* lookup = alloc_alloc_t(alloc, ScriptLookup);

  *lookup = (ScriptLookup){
      .alloc    = alloc,
      .lineEnds = dynarray_create_t(alloc, ScriptPos, 128),
  };

  return lookup;
}

void script_lookup_update(ScriptLookup* l, const String src) {
  if (src.size > l->srcBuffer.size) {
    string_maybe_free(l->alloc, l->srcBuffer);
    l->srcBuffer = alloc_alloc(l->alloc, bits_nextpow2(src.size), 1);
  }
  mem_cpy(l->srcBuffer, src);
  l->srcSize = src.size;

  dynarray_clear(&l->lineEnds);
  for (const u8* itr = string_begin(src); itr != string_end(src); ++itr) {
    if (*itr == '\n') {
      *dynarray_push_t(&l->lineEnds, ScriptPos) = (ScriptPos)(itr - string_begin(src));
    }
  }
}

String script_lookup_src(const ScriptLookup* l) {
  return string_slice(l->srcBuffer, 0, l->srcSize);
}

void script_lookup_destroy(ScriptLookup* l) {
  string_maybe_free(l->alloc, l->srcBuffer);
  dynarray_destroy(&l->lineEnds);
  alloc_free_t(l->alloc, l);
}

ScriptPosLineCol script_lookup_to_line_col(const ScriptLookup* l, const ScriptPos pos) {
  diag_assert(pos <= l->srcSize);

  const ScriptPos* linesBegin = dynarray_begin_t(&l->lineEnds, ScriptPos);
  const ScriptPos* linesEnd   = dynarray_end_t(&l->lineEnds, ScriptPos);

  const ScriptPos* nextLine =
      search_binary_greater_or_equal_t(linesBegin, linesEnd, ScriptPos, compare_u32, &pos);

  const ScriptPos* nextLineOrEnd = nextLine ? nextLine : linesEnd;

  u16       line       = 0;
  ScriptPos lineOffset = 0;
  if (nextLineOrEnd != linesBegin) {
    line       = (u16)(nextLineOrEnd - linesBegin);
    lineOffset = *(nextLineOrEnd - 1) + 1; // +1 to skip over the newline character.
  }

  return (ScriptPosLineCol){
      .line   = line,
      .column = (u32)utf8_cp_count(string_slice(l->srcBuffer, lineOffset, pos - lineOffset)),
  };
}

ScriptPos script_lookup_from_line_col(const ScriptLookup* l, const ScriptPosLineCol lc) {
  if (UNLIKELY(lc.line > l->lineEnds.size)) {
    return script_pos_sentinel;
  }

  ScriptPos currentPos = 0;
  if (lc.line) {
    currentPos = *dynarray_at_t(&l->lineEnds, lc.line - 1, ScriptPos) + 1; // +1 for newline char.
  }

  // Advance 'lc.column' columns.
  for (u16 col = 0; col != lc.column; ++col) {
    if (UNLIKELY(currentPos >= l->srcSize)) {
      return script_pos_sentinel;
    }
    const u8 ch = *string_at(l->srcBuffer, currentPos);
    currentPos += (u32)math_max(utf8_cp_bytes_from_first(ch), 1);
  }

  return currentPos;
}

ScriptRangeLineCol script_lookup_range_to_line_col(const ScriptLookup* l, const ScriptRange range) {
  return (ScriptRangeLineCol){
      .start = script_lookup_to_line_col(l, range.start),
      .end   = script_lookup_to_line_col(l, range.end),
  };
}

ScriptRange
script_lookup_range_from_line_col(const ScriptLookup* l, const ScriptRangeLineCol range) {
  return (ScriptRange){
      .start = script_lookup_from_line_col(l, range.start),
      .end   = script_lookup_from_line_col(l, range.end),
  };
}
