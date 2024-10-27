#include "core_diag.h"
#include "core_math.h"
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
