#pragma once
#include "core_string.h"

typedef u32 ScriptPos; // Bytes into the source text.

typedef struct {
  ScriptPos start, end;
} ScriptPosRange;

typedef struct {
  u16 line, column; // 0 based.
} ScriptPosHuman;

ScriptPosRange script_pos_range(ScriptPos start, ScriptPos end);
ScriptPos      script_pos_trim(String sourceText, ScriptPos);
ScriptPosHuman script_pos_humanize(String sourceText, ScriptPos);
