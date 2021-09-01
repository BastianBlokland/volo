#include "core_diag.h"
#include "core_env.h"

#include <stdlib.h>

#define env_var_max_name_size 256

bool env_var(String name, DynString* output) {

  if (UNLIKELY(name.size >= env_var_max_name_size)) {
    diag_assert_fail(
        "Environment variable name with length {} exceeds maximum of {}",
        fmt_int(name.size),
        fmt_int(env_var_max_name_size));
    return false;
  }

  // Copy the name on the stack and null-terminate it.
  Mem pathBuffer = mem_stack(env_var_max_name_size);
  mem_cpy(pathBuffer, name);
  *mem_at_u8(pathBuffer, name.size) = '\0';

  const char* res = getenv((const char*)pathBuffer.ptr);
  if (!res) {
    return false;
  }

  if (output) {
    dynstring_append(output, string_from_null_term(res));
  }

  return true;
}
