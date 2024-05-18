#include "check_spec.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "script_mem.h"

#include "utils_internal.h"

spec(mem) {
  ScriptMem m;

  setup() { m = script_mem_create(g_allocHeap); }

  it("returns null if the value is unset") {
    check_eq_val(script_mem_load(&m, string_hash_lit("test1")), script_null());
    check_eq_val(script_mem_load(&m, string_hash_lit("test2")), script_null());

    script_mem_store(&m, string_hash_lit("test1"), script_num(42));

    check_eq_val(script_mem_load(&m, string_hash_lit("test1")), script_num(42));
    check_eq_val(script_mem_load(&m, string_hash_lit("test2")), script_null());

    script_mem_store(&m, string_hash_lit("test1"), script_null());

    check_eq_val(script_mem_load(&m, string_hash_lit("test1")), script_null());
    check_eq_val(script_mem_load(&m, string_hash_lit("test2")), script_null());
  }

  it("returns the stored value") {
    script_mem_store(&m, string_hash_lit("test"), script_num(42));
    check_eq_float(script_get_num(script_mem_load(&m, string_hash_lit("test")), 0), 42, 1e-6f);
  }

  it("can store many value keys") {
    enum { KeyCount = 1337 };
    for (u32 i = 0; i != KeyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      script_mem_store(&m, string_hash(key), script_num(i));
    }
    for (u32 i = 0; i != KeyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      check_eq_val(script_mem_load(&m, string_hash(key)), script_num(i));
    }
  }

  it("can unset value") {
    check_eq_val(script_mem_load(&m, string_hash_lit("test")), script_null());

    script_mem_store(&m, string_hash_lit("test"), script_num(42));
    check_eq_val(script_mem_load(&m, string_hash_lit("test")), script_num(42));

    script_mem_store(&m, string_hash_lit("test"), script_null());
    check_eq_val(script_mem_load(&m, string_hash_lit("test")), script_null());
  }

  it("can update previously unset value") {
    script_mem_store(&m, string_hash_lit("test"), script_num(42));
    script_mem_store(&m, string_hash_lit("test"), script_null());

    check_eq_val(script_mem_load(&m, string_hash_lit("test")), script_null());

    script_mem_store(&m, string_hash_lit("test"), script_num(42));

    check_eq_val(script_mem_load(&m, string_hash_lit("test")), script_num(42));
  }

  it("can iterate an empty memory instance") {
    const ScriptMemItr itr = script_mem_begin(&m);
    check_eq_int(itr.key, 0);
    check_eq_int(itr.next, sentinel_u32);
  }

  it("can iterate memory keys") {
    enum { KeyCount = 1337 };
    for (u32 i = 0; i != KeyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      script_mem_store(&m, string_hash(key), script_num(i));
    }

    const u8     seenValsBits[bits_to_bytes(KeyCount) + 1] = {0};
    const BitSet seenVals                                  = bitset_from_array(seenValsBits);

    for (ScriptMemItr it = script_mem_begin(&m); it.key; it = script_mem_next(&m, it)) {
      const f64 val = script_get_num(script_mem_load(&m, it.key), 0);
      bitset_set(seenVals, (usize)val);
    }

    check_eq_int(bitset_count(seenVals), KeyCount);
  }

  teardown() { script_mem_destroy(&m); }
}
