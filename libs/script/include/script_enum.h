#pragma once
#include "core_annotation.h"
#include "core_string.h"

// Forward declare from 'script_panic.h'.
typedef struct sScriptPanicHandler ScriptPanicHandler;

#define script_enum_max_entries 16

typedef struct sScriptEnum {
  ALIGNAS(16) StringHash nameHashes[script_enum_max_entries];
  ALIGNAS(16) i32 values[script_enum_max_entries];
} ScriptEnum;

void script_enum_push(ScriptEnum*, String name, i32 value);

bool script_enum_contains_name(const ScriptEnum*, StringHash nameHash);

StringHash script_enum_lookup_name(const ScriptEnum*, i32 value);

i32 script_enum_lookup_maybe_value(const ScriptEnum*, StringHash nameHash, i32 def);

i32 script_enum_lookup_value(const ScriptEnum*, StringHash nameHash, ScriptPanicHandler*);

i32 script_enum_lookup_value_at_index(
    const ScriptEnum*, StringHash nameHash, u16 argIndex, ScriptPanicHandler*);
