#include "core_diag.h"
#include "core_stringtable.h"
#include "script_enum.h"

void script_enum_push(ScriptEnum* e, const String name) {
  diag_assert_msg(e->count < script_enum_max_entries, "ScriptEnum entry count exceeds max");
  e->hashes[e->count++] = stringtable_add(g_stringtable, name);
}

u32 script_enum_lookup(const ScriptEnum* e, const StringHash hash) {
  for (u32 i = 0; i != e->count; ++i) {
    if (e->hashes[i] == hash) {
      return i;
    }
  }
  return sentinel_u32;
}
