#pragma once
#include "core_string.h"

typedef u32 ScriptPos; // Bytes into the source text.

#define script_pos_sentinel sentinel_u32
#define script_range_sentinel ((ScriptRange){sentinel_u32, sentinel_u32})

typedef struct {
  u16 line, column; // 0 based.
} ScriptPosLineCol;

typedef struct sScriptRange {
  ScriptPos start, end;
} ScriptRange;

typedef struct {
  ScriptPosLineCol start, end;
} ScriptRangeLineCol;

ScriptPos        script_pos_trim(String src, ScriptPos);
ScriptPosLineCol script_pos_to_line_col(String src, ScriptPos);
ScriptPos        script_pos_from_line_col(String src, ScriptPosLineCol);

ScriptRange        script_range(ScriptPos start, ScriptPos end);
bool               script_range_contains(ScriptRange, ScriptPos);
bool               script_range_contains_range(ScriptRange, ScriptRange);
ScriptRange        script_range_full(String src);
String             script_range_text(String src, ScriptRange);
ScriptRangeLineCol script_range_to_line_col(String src, ScriptRange);
ScriptRange        script_range_from_line_col(String src, ScriptRangeLineCol);
