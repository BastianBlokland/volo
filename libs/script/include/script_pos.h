#pragma once
#include "core_string.h"

typedef u32 ScriptPos; // Bytes into the source text.

#define script_pos_sentinel sentinel_u32

typedef struct {
  ScriptPos start, end;
} ScriptPosRange;

typedef struct {
  u16 line, column; // 0 based.
} ScriptPosLineCol;

typedef struct {
  ScriptPosLineCol start, end;
} ScriptPosRangeLineCol;

ScriptPosRange        script_pos_range(ScriptPos start, ScriptPos end);
ScriptPosRange        script_pos_range_full(String sourceText);
String                script_pos_range_text(String sourceText, ScriptPosRange);
ScriptPos             script_pos_trim(String sourceText, ScriptPos);
ScriptPosLineCol      script_pos_to_line_col(String sourceText, ScriptPos);
ScriptPos             script_pos_from_line_col(String sourceText, ScriptPosLineCol);
ScriptPosRangeLineCol script_pos_range_to_line_col(String sourceText, ScriptPosRange);
ScriptPosRange        script_pos_range_from_line_col(String sourceText, ScriptPosRangeLineCol);
