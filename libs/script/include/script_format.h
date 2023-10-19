#pragma once
#include "core_dynstring.h"

typedef struct {
  u32 indentSize;
} ScriptFormatSettings;

/**
 * Reformat the given script source text.
 */
void script_format(DynString* out, String input, const ScriptFormatSettings*);
