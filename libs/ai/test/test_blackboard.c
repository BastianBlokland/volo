#include "ai_blackboard.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_bits.h"

#include "utils_internal.h"

spec(blackboard) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("returns null if the knowledge is unset") {
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test1")), ai_value_null());
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test2")), ai_value_null());

    ai_blackboard_set(bb, string_hash_lit("test1"), ai_value_number(42));

    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test1")), ai_value_number(42));
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test2")), ai_value_null());

    ai_blackboard_set_null(bb, string_hash_lit("test1"));

    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test1")), ai_value_null());
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test2")), ai_value_null());
  }

  it("returns the stored knowledge") {
    ai_blackboard_set(bb, string_hash_lit("test"), ai_value_number(42));
    check_eq_float(
        ai_value_get_number(ai_blackboard_get(bb, string_hash_lit("test")), 0), 42, 1e-6f);
  }

  it("can store many knowledge keys") {
    enum { KeyCount = 1337 };
    for (u32 i = 0; i != KeyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      ai_blackboard_set(bb, string_hash(key), ai_value_number(i));
    }
    for (u32 i = 0; i != KeyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      check_eq_value(ai_blackboard_get(bb, string_hash(key)), ai_value_number(i));
    }
  }

  it("can unset knowledge") {
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_null());

    ai_blackboard_set(bb, string_hash_lit("test"), ai_value_number(42));
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_number(42));

    ai_blackboard_set_null(bb, string_hash_lit("test"));
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_null());
  }

  it("can update previously unset knowledge") {
    ai_blackboard_set(bb, string_hash_lit("test"), ai_value_number(42));
    ai_blackboard_set_null(bb, string_hash_lit("test"));

    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_null());

    ai_blackboard_set(bb, string_hash_lit("test"), ai_value_number(42));

    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_number(42));
  }

  it("can iterate an empty blackboard") {
    const AiBlackboardItr itr = ai_blackboard_begin(bb);
    check_eq_int(itr.key, 0);
    check_eq_int(itr.next, sentinel_u32);
  }

  it("can iterate blackboard keys") {
    enum { KeyCount = 1337 };
    for (u32 i = 0; i != KeyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      ai_blackboard_set(bb, string_hash(key), ai_value_number(i));
    }

    const u8     seenValsBits[bits_to_bytes(KeyCount) + 1] = {0};
    const BitSet seenVals                                  = bitset_from_array(seenValsBits);

    for (AiBlackboardItr it = ai_blackboard_begin(bb); it.key; it = ai_blackboard_next(bb, it)) {
      const f64 val = ai_value_get_number(ai_blackboard_get(bb, it.key), 0);
      bitset_set(seenVals, (usize)val);
    }

    check_eq_int(bitset_count(seenVals), KeyCount);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
