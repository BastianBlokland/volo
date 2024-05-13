#include "check_spec.h"
#include "core_alloc.h"
#include "core_stringtable.h"

spec(stringtable) {
  StringTable* table;

  setup() { table = stringtable_create(g_allocHeap); }

  it("can lookup strings from hashes") {
    const String str = string_lit("Hello World");

    check_eq_int(stringtable_count(table), 0);
    const StringHash hash = stringtable_add(table, str);
    check_eq_int(stringtable_count(table), 1);

    check_eq_string(stringtable_lookup(table, hash), str);
  }

  it("can store many strings") {
    const u32 count = 267;

    // Add all strings.
    for (u32 i = 0; i != count; ++i) {
      const String str = fmt_write_scratch("My String {}", fmt_int(i));
      stringtable_add(table, str);
    }

    check_eq_int(stringtable_count(table), count);

    // Retrieve all strings.
    for (u32 i = 0; i != count; ++i) {
      const String     str  = fmt_write_scratch("My String {}", fmt_int(i));
      const StringHash hash = string_hash(str);
      check_eq_string(stringtable_lookup(table, hash), str);
    }
  }

  teardown() { stringtable_destroy(table); }
}
