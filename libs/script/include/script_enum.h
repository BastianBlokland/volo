#pragma once
#include "core_string.h"

#define script_enum_max_entries 8

typedef struct sScriptEnum {
  u32        count;
  StringHash hashes[script_enum_max_entries];
} ScriptEnum;

void script_enum_push(ScriptEnum*, String);
u32  script_enum_lookup(const ScriptEnum*, StringHash);
