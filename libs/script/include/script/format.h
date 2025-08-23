#pragma once
#include "core/forward.h"

typedef struct sScriptFormatSettings {
  u32 indentSize;
} ScriptFormatSettings;

/**
 * Reformat the given script source text.
 */
void script_format(DynString* out, String input, const ScriptFormatSettings*);
