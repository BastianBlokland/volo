#pragma once
#include "core_string.h"

#define script_enum_max_entries 8

typedef struct sScriptEnum {
  u32        count;
  StringHash hashes[script_enum_max_entries];
} ScriptEnum;

#define script_enum_push_lit(_ENUM_, _LIT_)                                                        \
  script_enum_push((_ENUM_), string_hash(string_lit(_LIT_)))

void script_enum_push(ScriptEnum*, StringHash);
u32  script_enum_lookup(const ScriptEnum*, StringHash);
