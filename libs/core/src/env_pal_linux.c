#include "core/alloc.h"
#include "core/diag.h"
#include "core/dynstring.h"
#include "core/env.h"

#include <errno.h>
#include <stdlib.h>

#define env_var_max_name_size 256
#define env_var_max_value_size (usize_kibibyte * 32)

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

bool env_var(String name, DynString* output) {
  if (UNLIKELY(name.size >= env_var_max_name_size)) {
    diag_assert_fail(
        "Environment variable name with length {} exceeds maximum of {}",
        fmt_int(name.size),
        fmt_int(env_var_max_name_size));
    return false;
  }

  const char* res = getenv(to_null_term_scratch(name));
  if (!res) {
    return false;
  }

  if (output) {
    const String resStr = string_from_null_term(res);
    if (resStr.size > env_var_max_value_size) {
      dynstring_append(output, string_slice(resStr, 0, env_var_max_value_size));
    } else {
      dynstring_append(output, resStr);
    }
  }

  return true;
}

void env_var_set(const String name, const String value) {
  if (UNLIKELY(name.size >= env_var_max_name_size)) {
    diag_assert_fail(
        "Environment variable name with length {} exceeds maximum of {}",
        fmt_int(name.size),
        fmt_int(env_var_max_name_size));
    return;
  }
  if (UNLIKELY(value.size >= env_var_max_value_size)) {
    diag_assert_fail(
        "Environment variable value with length {} exceeds maximum of {}",
        fmt_int(value.size),
        fmt_int(env_var_max_value_size));
    return;
  }
  if (UNLIKELY(setenv(to_null_term_scratch(name), to_null_term_scratch(value), 1 /* replace */))) {
    diag_crash_msg("setenv() failed: {}", fmt_int(errno));
  }
}

void env_var_clear(const String name) {
  if (UNLIKELY(name.size >= env_var_max_name_size)) {
    diag_assert_fail(
        "Environment variable name with length {} exceeds maximum of {}",
        fmt_int(name.size),
        fmt_int(env_var_max_name_size));
    return;
  }
  if (UNLIKELY(unsetenv(to_null_term_scratch(name)))) {
    diag_crash_msg("unsetenv() failed: {}", fmt_int(errno));
  }
}
