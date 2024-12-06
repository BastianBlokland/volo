#pragma once
#include "core.h"

typedef u32 ScriptPos; // Bytes into the source text.

#define script_pos_sentinel sentinel_u32
#define script_range_sentinel ((ScriptRange){sentinel_u32, sentinel_u32})

typedef struct {
  u16 line, column; // 0 based.
} ScriptPosLineCol;

typedef struct sScriptRange {
  ScriptPos start, end;
} ScriptRange;

typedef struct sScriptRangeLineCol {
  ScriptPosLineCol start, end;
} ScriptRangeLineCol;

ScriptPos        script_pos_trim(String src, ScriptPos);
ScriptPosLineCol script_pos_to_line_col(String src, ScriptPos);
ScriptPos        script_pos_from_line_col(String src, ScriptPosLineCol);

ScriptRange        script_range(ScriptPos start, ScriptPos end);
bool               script_range_valid(ScriptRange);
bool               script_range_contains(ScriptRange, ScriptPos);
bool               script_range_subrange(ScriptRange, ScriptRange);
ScriptRange        script_range_full(String src);
String             script_range_text(String src, ScriptRange);
ScriptRangeLineCol script_range_to_line_col(String src, ScriptRange);
ScriptRange        script_range_from_line_col(String src, ScriptRangeLineCol);

/**
 * Helper to speed-up looking-up positions in the given source text.
 */
typedef struct sScriptLookup ScriptLookup;

ScriptLookup*      script_lookup_create(Allocator*);
void               script_lookup_update(ScriptLookup*, String src);
void               script_lookup_update_range(ScriptLookup*, String src, ScriptRange);
String             script_lookup_src(const ScriptLookup*);
String             script_lookup_src_range(const ScriptLookup*, ScriptRange);
void               script_lookup_destroy(ScriptLookup*);
ScriptPosLineCol   script_lookup_to_line_col(const ScriptLookup*, ScriptPos);
ScriptPos          script_lookup_from_line_col(const ScriptLookup*, ScriptPosLineCol);
ScriptRangeLineCol script_lookup_range_to_line_col(const ScriptLookup*, ScriptRange);
ScriptRange        script_lookup_range_from_line_col(const ScriptLookup*, ScriptRangeLineCol);
