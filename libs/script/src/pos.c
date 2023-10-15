#include "core_diag.h"
#include "core_math.h"
#include "core_utf8.h"
#include "script_lex.h"
#include "script_pos.h"

ScriptPosRange script_pos_range(const ScriptPos start, const ScriptPos end) {
  diag_assert(end >= start);
  return (ScriptPosRange){.start = start, .end = end};
}

ScriptPosRange script_pos_range_full(String sourceText) {
  return script_pos_range(0, (u32)sourceText.size);
}

String script_pos_range_text(const String sourceText, const ScriptPosRange range) {
  diag_assert(range.end >= range.start);
  return string_slice(sourceText, range.start, range.end - range.start);
}

ScriptPos script_pos_trim(const String sourceText, const ScriptPos pos) {
  const String toEnd        = string_consume(sourceText, pos);
  const String toEndTrimmed = script_lex_trim(toEnd);
  return (ScriptPos)(sourceText.size - toEndTrimmed.size);
}

ScriptPosLineCol script_pos_to_line_col(const String sourceText, const ScriptPos pos) {
  diag_assert(pos <= sourceText.size);
  u32 currentPos = 0;
  u16 line = 0, column = 0;
  while (currentPos < pos) {
    const u8 ch = *string_at(sourceText, currentPos);
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

ScriptPos script_pos_from_line_col(const String sourceText, const ScriptPosLineCol lc) {
  u32 currentPos = 0;

  // Advance 'lc.line' lines.
  for (u16 line = 0; line != lc.line; ++line) {
    // Advance until the end of the line.
    for (;;) {
      if (UNLIKELY(currentPos == sourceText.size)) {
        return script_pos_sentinel;
      }
      const u8 ch = *string_at(sourceText, currentPos);
      ++currentPos;
      if (ch == '\n') {
        break;
      }
    }
  }

  // Advance 'lc.column' columns.
  for (u16 col = 0; col != lc.column; ++col) {
    if (UNLIKELY(currentPos >= sourceText.size)) {
      return script_pos_sentinel;
    }
    const u8 ch = *string_at(sourceText, currentPos);
    currentPos += (u32)math_max(utf8_cp_bytes_from_first(ch), 1);
  }

  return currentPos;
}
