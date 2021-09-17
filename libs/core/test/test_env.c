#include "check_spec.h"
#include "core_alloc.h"
#include "core_env.h"

spec(env) {

  it("can check if an environment variable exists") {
    check(env_var(string_lit("PATH"), null));
    check(!env_var(string_lit("NON_EXISTING_ENVIRONMENT_VARIABLE_42"), null));
  }

  it("can read the value of an environment variable") {
    DynString buffer = dynstring_create(g_alloc_heap, usize_kibibyte);

    check(env_var(string_lit("PATH"), &buffer));
    check(!string_is_empty(dynstring_view(&buffer)));

    dynstring_destroy(&buffer);
  }
}
