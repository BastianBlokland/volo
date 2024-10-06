#include "core_diag.h"
#include "core_stringtable.h"
#include "script_enum.h"
#include "script_error.h"

void script_enum_push(ScriptEnum* e, const String name, const i32 value) {
  const StringHash nameHash = stringtable_add(g_stringtable, name);

  diag_assert_msg(e->count < script_enum_max_entries, "ScriptEnum entry count exceeds max");
  diag_assert_msg(!script_enum_contains(e, nameHash), "Duplicate name in ScriptEnum");

  e->nameHashes[e->count] = nameHash;
  e->values[e->count]     = value;
  ++e->count;
}

bool script_enum_contains(const ScriptEnum* e, const StringHash nameHash) {
  for (u32 i = 0; i != e->count; ++i) {
    if (e->nameHashes[i] == nameHash) {
      return e->values[i];
    }
  }
  return false;
}

i32 script_enum_lookup_value(const ScriptEnum* e, const StringHash nameHash, ScriptError* err) {
  for (u32 i = 0; i != e->count; ++i) {
    if (e->nameHashes[i] == nameHash) {
      return e->values[i];
    }
  }
  return *err = script_error(ScriptError_EnumInvalidEntry), 0;
}

i32 script_enum_lookup_maybe_value(const ScriptEnum* e, const StringHash nameHash, const i32 def) {
  for (u32 i = 0; i != e->count; ++i) {
    if (e->nameHashes[i] == nameHash) {
      return e->values[i];
    }
  }
  return def;
}

StringHash script_enum_lookup_name(const ScriptEnum* e, const i32 value) {
  for (u32 i = 0; i != e->count; ++i) {
    if (e->values[i] == value) {
      return e->nameHashes[i];
    }
  }
  return 0;
}
