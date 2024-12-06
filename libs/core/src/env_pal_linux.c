#include "core_diag.h"
#include "core_dynstring.h"
#include "core_env.h"

#include <stdlib.h>

#define env_var_max_name_size 256
#define env_var_max_value_size (usize_kibibyte * 32)

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
    const String resStr = string_from_null_term(res);
    if (resStr.size > env_var_max_value_size) {
      dynstring_append(output, string_slice(resStr, 0, env_var_max_value_size));
    } else {
      dynstring_append(output, resStr);
    }
  }

  return true;
}
