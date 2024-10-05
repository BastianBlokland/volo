#include "check_spec.h"
#include "script_enum.h"
#include "script_error.h"

spec(enum_) {
  it("can lookup values") {
    ScriptEnum enum_ = {0};
    script_enum_push(&enum_, string_lit("a"), 1);
    script_enum_push(&enum_, string_lit("b"), 42);
    script_enum_push(&enum_, string_lit("c"), 1337);

    ScriptError err = {0};
    check_eq_int(script_enum_lookup_value(&enum_, string_hash_lit("a"), &err), 1);
    check_eq_int(err.kind, ScriptError_None);

    check_eq_int(script_enum_lookup_value(&enum_, string_hash_lit("b"), &err), 42);
    check_eq_int(err.kind, ScriptError_None);

    check_eq_int(script_enum_lookup_value(&enum_, string_hash_lit("c"), &err), 1337);
    check_eq_int(err.kind, ScriptError_None);
  }

  it("can optionally lookup a value") {
    ScriptEnum enum_ = {0};
    script_enum_push(&enum_, string_lit("a"), 1);
    script_enum_push(&enum_, string_lit("b"), 42);
    script_enum_push(&enum_, string_lit("c"), 1337);

    check_eq_int(script_enum_lookup_maybe_value(&enum_, string_hash_lit("a"), -1), 1);
    check_eq_int(script_enum_lookup_maybe_value(&enum_, string_hash_lit("b"), -1), 42);
    check_eq_int(script_enum_lookup_maybe_value(&enum_, string_hash_lit("c"), -1), 1337);
    check_eq_int(script_enum_lookup_maybe_value(&enum_, string_hash_lit("d"), -1), -1);
  }

  it("fails when looking up a non-existing value") {
    ScriptEnum enum_ = {0};
    script_enum_push(&enum_, string_lit("a"), 1);
    script_enum_push(&enum_, string_lit("b"), 42);
    script_enum_push(&enum_, string_lit("c"), 1337);

    ScriptError err = {0};
    script_enum_lookup_value(&enum_, string_hash_lit("d"), &err);
    check_eq_int(err.kind, ScriptError_EnumInvalidEntry);
  }

  it("can lookup names") {
    ScriptEnum enum_ = {0};
    script_enum_push(&enum_, string_lit("a"), 1);
    script_enum_push(&enum_, string_lit("b"), 42);
    script_enum_push(&enum_, string_lit("c"), 1337);

    check_eq_int(script_enum_lookup_name(&enum_, -1), 0);
    check_eq_int(script_enum_lookup_name(&enum_, 0), 0);
    check_eq_int(script_enum_lookup_name(&enum_, 1), string_hash_lit("a"));
    check_eq_int(script_enum_lookup_name(&enum_, 41), 0);
    check_eq_int(script_enum_lookup_name(&enum_, 42), string_hash_lit("b"));
    check_eq_int(script_enum_lookup_name(&enum_, 1337), string_hash_lit("c"));
  }
}
