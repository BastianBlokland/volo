#include "core_diag.h"
#include "core_math.h"
#include "core_utf8.h"
#include "script_lex.h"
#include "script_pos.h"

ScriptPosRange script_pos_range(const ScriptPos start, const ScriptPos end) {
  diag_assert(end >= start);
  return (ScriptPosRange){.start = start, .end = end};
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
