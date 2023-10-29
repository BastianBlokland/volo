#pragma once
#include "core_string.h"

// Forward declare from 'script_error.h'.
typedef struct sScriptError ScriptError;

#define script_enum_max_entries 8

typedef struct sScriptEnum {
  u32        count;
  StringHash nameHashes[script_enum_max_entries];
  i32        values[script_enum_max_entries];
} ScriptEnum;

void script_enum_push(ScriptEnum*, String name, i32 value);

i32        script_enum_lookup_value(const ScriptEnum*, StringHash nameHash, ScriptError*);
i32        script_enum_lookup_maybe_value(const ScriptEnum*, StringHash nameHash, i32 def);
StringHash script_enum_lookup_name(const ScriptEnum*, i32 value);
