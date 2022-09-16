#include "ai_blackboard.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(blackboard) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("returns 0 if the knowledge is unset") {
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 0, 1e-6f);
  }

  it("returns the stored knowledge") {
    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42, 1e-6f);
  }

  it("can store many knowledge keys") {
    const u32 keyCount = 1337;
    for (u32 i = 0; i != keyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      ai_blackboard_set_f64(bb, string_hash(key), i);
    }
    for (u32 i = 0; i != keyCount; ++i) {
      const String key = fmt_write_scratch("test_{}", fmt_int(i));
      check_eq_float(ai_blackboard_get_f64(bb, string_hash(key)), i, 1e-6f);
    }
  }

  teardown() { ai_blackboard_destroy(bb); }
}
