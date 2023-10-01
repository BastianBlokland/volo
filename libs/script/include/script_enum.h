#pragma once
#include "core_string.h"

#define script_enum_max_entries 8

typedef struct sScriptEnum {
  u32        count;
  StringHash nameHashes[script_enum_max_entries];
  i32        values[script_enum_max_entries];
} ScriptEnum;

void script_enum_push(ScriptEnum*, String name, i32 value);
i32  script_enum_lookup(const ScriptEnum*, StringHash nameHash, i32 value);
