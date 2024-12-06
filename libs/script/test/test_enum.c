#include "check_spec.h"
#include "script_enum.h"

spec(enum_) {
  it("can check if it contains a name") {
    ScriptEnum e = {0};
    script_enum_push(&e, string_lit("a"), 1);
    script_enum_push(&e, string_lit("b"), 42);
    script_enum_push(&e, string_lit("c"), 1337);
    script_enum_push(&e, string_lit("d"), 1337);
    script_enum_push(&e, string_lit("e"), 1337);
    script_enum_push(&e, string_lit("f"), 1337);
    script_enum_push(&e, string_lit("g"), 1337);
    script_enum_push(&e, string_lit("h"), 1337);
    script_enum_push(&e, string_lit("i"), 1337);

    check(script_enum_contains_name(&e, string_hash_lit("a")));
    check(script_enum_contains_name(&e, string_hash_lit("b")));
    check(script_enum_contains_name(&e, string_hash_lit("c")));
    check(script_enum_contains_name(&e, string_hash_lit("d")));
    check(script_enum_contains_name(&e, string_hash_lit("e")));
    check(script_enum_contains_name(&e, string_hash_lit("f")));
    check(script_enum_contains_name(&e, string_hash_lit("g")));
    check(script_enum_contains_name(&e, string_hash_lit("h")));
    check(script_enum_contains_name(&e, string_hash_lit("i")));
    check(!script_enum_contains_name(&e, string_hash_lit("j")));
  }

  it("can lookup values") {
    ScriptEnum e = {0};
    script_enum_push(&e, string_lit("a"), 1);
    script_enum_push(&e, string_lit("b"), 42);
    script_enum_push(&e, string_lit("c"), 1337);

    check_eq_int(script_enum_lookup_value(&e, string_hash_lit("a"), null), 1);
    check_eq_int(script_enum_lookup_value(&e, string_hash_lit("b"), null), 42);
    check_eq_int(script_enum_lookup_value(&e, string_hash_lit("c"), null), 1337);
  }

  it("can optionally lookup a value") {
    ScriptEnum e = {0};
    script_enum_push(&e, string_lit("a"), 1);
    script_enum_push(&e, string_lit("b"), 42);
    script_enum_push(&e, string_lit("c"), 1337);

    check_eq_int(script_enum_lookup_maybe_value(&e, string_hash_lit("a"), -1), 1);
    check_eq_int(script_enum_lookup_maybe_value(&e, string_hash_lit("b"), -1), 42);
    check_eq_int(script_enum_lookup_maybe_value(&e, string_hash_lit("c"), -1), 1337);
    check_eq_int(script_enum_lookup_maybe_value(&e, string_hash_lit("d"), -1), -1);
  }

  it("can lookup names") {
    ScriptEnum e = {0};
    script_enum_push(&e, string_lit("a"), 1);
    script_enum_push(&e, string_lit("b"), 42);
    script_enum_push(&e, string_lit("c"), 1337);

    check_eq_int(script_enum_lookup_name(&e, -1), 0);
    check_eq_int(script_enum_lookup_name(&e, 0), 0);
    check_eq_int(script_enum_lookup_name(&e, 1), string_hash_lit("a"));
    check_eq_int(script_enum_lookup_name(&e, 41), 0);
    check_eq_int(script_enum_lookup_name(&e, 42), string_hash_lit("b"));
    check_eq_int(script_enum_lookup_name(&e, 1337), string_hash_lit("c"));
  }
}
