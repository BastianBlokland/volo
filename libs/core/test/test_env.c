#include "check/spec.h"
#include "core/alloc.h"
#include "core/dynstring.h"
#include "core/env.h"

spec(env) {

  it("can check if an environment variable exists") {
    check(env_var(string_lit("PATH"), null));
    check(!env_var(string_lit("NON_EXISTING_ENVIRONMENT_VARIABLE_42"), null));
  }

  it("can read the value of an environment variable") {
    DynString buffer = dynstring_create(g_allocHeap, usize_kibibyte);

    check(env_var(string_lit("PATH"), &buffer));
    check(!string_is_empty(dynstring_view(&buffer)));

    dynstring_destroy(&buffer);
  }

  it("can write an environment variable") {
    const String varName = string_lit("VOLO_TEST_ENV_VAR_1");
    const String varVal  = string_lit("Hello world!");

    env_var_set(varName, varVal);

    DynString buffer = dynstring_create(g_allocHeap, usize_kibibyte);

    check(env_var(varName, &buffer));
    check_eq_string(dynstring_view(&buffer), varVal);

    dynstring_destroy(&buffer);
  }

  it("can clear an environment variable") {
    const String varName = string_lit("VOLO_TEST_ENV_VAR_2");
    const String varVal  = string_lit("Hello world!");

    env_var_set(varName, varVal);
    check_eq_string(env_var_scratch(varName), varVal);

    env_var_clear(varName);
    check_eq_string(env_var_scratch(varName), string_empty);
  }
}
