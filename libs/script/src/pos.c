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

struct sScriptPosLookup {
  Allocator* alloc;
  usize      srcSize;
  String     srcBuffer;
  DynArray   lineEnds; // ScriptPos[], sorted positions in the source where a line ends.
};

ScriptPosLookup* script_pos_lookup_create(Allocator* alloc) {
  ScriptPosLookup* lookup = alloc_alloc_t(alloc, ScriptPosLookup);

  *lookup = (ScriptPosLookup){
      .alloc    = alloc,
      .lineEnds = dynarray_create_t(alloc, ScriptPos, 128),
  };

  return lookup;
}

void script_pos_lookup_update(ScriptPosLookup* lookup, const String src) {
  if (src.size > lookup->srcBuffer.size) {
    string_maybe_free(lookup->alloc, lookup->srcBuffer);
    lookup->srcBuffer = alloc_alloc(lookup->alloc, bits_nextpow2(src.size), 1);
  }
  mem_cpy(lookup->srcBuffer, src);
  lookup->srcSize = src.size;

  dynarray_clear(&lookup->lineEnds);
  for (const u8* itr = string_begin(src); itr != string_end(src); ++itr) {
    if (*itr == '\n') {
      *dynarray_push_t(&lookup->lineEnds, ScriptPos) = (ScriptPos)(itr - string_begin(src));
    }
  }
}

String script_pos_lookup_src(const ScriptPosLookup* lookup) {
  return string_slice(lookup->srcBuffer, 0, lookup->srcSize);
}

void script_pos_lookup_destroy(ScriptPosLookup* lookup) {
  string_maybe_free(lookup->alloc, lookup->srcBuffer);
  dynarray_destroy(&lookup->lineEnds);
  alloc_free_t(lookup->alloc, lookup);
}

ScriptPosLineCol script_pos_lookup_to_line_col(const ScriptPosLookup* lookup, const ScriptPos pos) {
  diag_assert(pos <= lookup->srcSize);

  const ScriptPos* linesBegin = dynarray_begin_t(&lookup->lineEnds, ScriptPos);
  const ScriptPos* linesEnd   = dynarray_end_t(&lookup->lineEnds, ScriptPos);

  const ScriptPos* nextLinePtr =
      search_binary_greater_t(linesBegin, linesEnd, ScriptPos, compare_u32, &pos);

  const ScriptPos lineOffset = nextLinePtr && (nextLinePtr != linesBegin) ? 0 : *(linesBegin - 1);
  const String    lineSrc    = string_slice(lookup->srcBuffer, lineOffset, pos - lineOffset);

  return (ScriptPosLineCol){
      .line   = nextLinePtr ? (u32)(nextLinePtr - linesBegin) : 0,
      .column = (u32)utf8_cp_count(lineSrc),
  };
}

ScriptPos
script_pos_lookup_from_line_col(const ScriptPosLookup* lookup, const ScriptPosLineCol lc) {
  if (UNLIKELY(lc.line > lookup->lineEnds.size)) {
    return script_pos_sentinel;
  }

  ScriptPos currentPos = 0;
  if (lc.line) {
    currentPos = *dynarray_at_t(&lookup->lineEnds, lc.line - 1, ScriptPos);
  }

  // Advance 'lc.column' columns.
  for (u16 col = 0; col != lc.column; ++col) {
    if (UNLIKELY(currentPos >= lookup->srcSize)) {
      return script_pos_sentinel;
    }
    const u8 ch = *string_at(lookup->srcBuffer, currentPos);
    currentPos += (u32)math_max(utf8_cp_bytes_from_first(ch), 1);
  }

  return currentPos;
}

ScriptRangeLineCol
script_pos_lookup_range_to_line_col(const ScriptPosLookup* lookup, const ScriptRange range) {
  return (ScriptRangeLineCol){
      .start = script_pos_lookup_to_line_col(lookup, range.start),
      .end   = script_pos_lookup_to_line_col(lookup, range.end),
  };
}

ScriptRange script_pos_lookup_range_from_line_col(
    const ScriptPosLookup* lookup, const ScriptRangeLineCol range) {
  return (ScriptRange){
      .start = script_pos_lookup_from_line_col(lookup, range.start),
      .end   = script_pos_lookup_from_line_col(lookup, range.end),
  };
}
