#include "ai_blackboard.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_bits.h"

spec(blackboard) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("returns 0 if the knowledge is unset") {
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 0, 1e-6f);
  }

  it("can query the knowledge type") {
    check_eq_int(ai_blackboard_type(bb, string_hash_lit("test")), AiBlackboardType_Invalid);

    ai_blackboard_set_f64(bb, string_hash_lit("test1"), 42);
    check_eq_int(ai_blackboard_type(bb, string_hash_lit("test1")), AiBlackboardType_f64);

    ai_blackboard_set_vector(bb, string_hash_lit("test2"), geo_vector(1, 2, 3));
    check_eq_int(ai_blackboard_type(bb, string_hash_lit("test2")), AiBlackboardType_Vector);
  }

  it("can test if knowledge exists") {
    check(!ai_blackboard_exists(bb, string_hash_lit("test1")));
    check(!ai_blackboard_exists(bb, string_hash_lit("test2")));

    ai_blackboard_set_f64(bb, string_hash_lit("test1"), 42);

    check(ai_blackboard_exists(bb, string_hash_lit("test1")));
    check(!ai_blackboard_exists(bb, string_hash_lit("test2")));

    ai_blackboard_unset(bb, string_hash_lit("test1"));

    check(!ai_blackboard_exists(bb, string_hash_lit("test1")));
    check(!ai_blackboard_exists(bb, string_hash_lit("test2")));
  }

  it("returns the stored knowledge") {
    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42, 1e-6f);
  }

  it("can store many knowledge keys") {
    enum { KeyCount = 1337 };
    for (u32 i = 0; i != KeyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      ai_blackboard_set_f64(bb, string_hash(key), i);
    }
    for (u32 i = 0; i != KeyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      check_eq_float(ai_blackboard_get_f64(bb, string_hash(key)), i, 1e-6f);
    }
  }

  it("can copy a knowledge value to a new key") {
    ai_blackboard_set_f64(bb, string_hash_lit("test1"), 42);

    ai_blackboard_copy(bb, string_hash_lit("test1"), string_hash_lit("test2"));
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test2")), 42, 1e-6f);
  }

  it("can unset knowledge") {
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 0, 1e-6f);

    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42, 1e-6f);

    ai_blackboard_unset(bb, string_hash_lit("test"));
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 0, 1e-6f);
  }

  it("can update previously unset knowledge") {
    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);
    ai_blackboard_unset(bb, string_hash_lit("test"));

    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 0, 1e-6f);

    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);

    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42, 1e-6f);
  }

  it("can copy a knowledge value to an existing key") {
    ai_blackboard_set_f64(bb, string_hash_lit("test1"), 1);
    ai_blackboard_set_f64(bb, string_hash_lit("test2"), 2);

    ai_blackboard_copy(bb, string_hash_lit("test1"), string_hash_lit("test2"));
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test2")), 1, 1e-6f);
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
      ai_blackboard_set_f64(bb, string_hash(key), i);
    }

    const u8     seenValsBits[bits_to_bytes(KeyCount) + 1] = {0};
    const BitSet seenVals                                  = bitset_from_array(seenValsBits);

    for (AiBlackboardItr it = ai_blackboard_begin(bb); it.key; it = ai_blackboard_next(bb, it)) {
      const f64 val = ai_blackboard_get_f64(bb, it.key);
      bitset_set(seenVals, (usize)val);
    }

    check_eq_int(bitset_count(seenVals), KeyCount);
  }

  it("can check two knowledge values for equality") {
    const StringHash a = string_hash_lit("testA");
    const StringHash b = string_hash_lit("testB");
    const StringHash c = string_hash_lit("testC");
    check(!ai_blackboard_equals(bb, a, b));

    ai_blackboard_set_f64(bb, b, 42);
    check(!ai_blackboard_equals(bb, a, b));

    ai_blackboard_set_bool(bb, c, false);
    check(!ai_blackboard_equals(bb, a, c));

    ai_blackboard_set_f64(bb, a, 42);
    check(ai_blackboard_equals(bb, a, b));

    ai_blackboard_unset(bb, a);
    check(!ai_blackboard_equals(bb, a, b));
  }

  it("can check a knowledge value and a literal for equality") {
    const StringHash a = string_hash_lit("testA");
    const StringHash b = string_hash_lit("testB");
    check(!ai_blackboard_equals_f64(bb, a, 42));

    ai_blackboard_set_f64(bb, a, 42);
    check(ai_blackboard_equals_f64(bb, a, 42));

    ai_blackboard_set_f64(bb, b, false);
    check(!ai_blackboard_equals_f64(bb, b, 42));
  }

  teardown() { ai_blackboard_destroy(bb); }
}
