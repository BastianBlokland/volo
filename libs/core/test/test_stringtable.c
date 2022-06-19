#include "check_spec.h"
#include "core_alloc.h"
#include "core_stringtable.h"

spec(stringtable) {
  StringTable* table;

  setup() { table = stringtable_create(g_alloc_heap); }

  it("can lookup strings from hashes") {
    const String     str  = string_lit("Hello World");
    const StringHash hash = string_hash(str);
    stringtable_add(table, str);

    check_eq_string(stringtable_lookup(table, hash), str);
  }

  teardown() { stringtable_destroy(table); }
}
