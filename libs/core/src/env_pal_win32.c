#include "core/alloc.h"
#include "core/diag.h"
#include "core/dynstring.h"
#include "core/env.h"
#include "core/winutils.h"

#include <Windows.h>

#define env_var_max_name_size 256
#define env_var_max_value_size (usize_kibibyte * 32)

bool env_var(String name, DynString* output) {
  // Convert the name to a null-terminated wide-char string.
  const usize nameBufferSize = winutils_to_widestr_size(name);
  if (UNLIKELY(sentinel_check(nameBufferSize))) {
    // Name contains invalid utf8.
    // TODO: Decide if its worth crashing the program here or just returning false.
    return false;
  }
  if (UNLIKELY(nameBufferSize >= env_var_max_name_size)) {
    diag_assert_fail(
        "Environment variable name with length {} exceeds maximum of {}",
        fmt_int(nameBufferSize),
        fmt_int(env_var_max_name_size));
    return false;
  }
  Mem nameBufferMem = mem_stack(nameBufferSize);
  winutils_to_widestr(nameBufferMem, name);

  Mem         buffer         = alloc_alloc(g_allocScratch, env_var_max_value_size, 1);
  const DWORD bufferMaxChars = (DWORD)(buffer.size / sizeof(wchar_t));

  const DWORD wcharCount = GetEnvironmentVariable(
      (const wchar_t*)nameBufferMem.ptr, (wchar_t*)buffer.ptr, bufferMaxChars);

  if (wcharCount == 0) {
    // Environment variable does not exist.
    alloc_free(g_allocScratch, buffer);
    return false;
  }

  if (UNLIKELY(wcharCount >= bufferMaxChars)) {
    // Environment variable did not fit in our scratch buffer.
    // TODO: Decide if its worth crashing the program here or just returning false.
    alloc_free(g_allocScratch, buffer);
    return false;
  }

  if (output) {
    const usize outputSize = winutils_from_widestr_size(buffer.ptr, (usize)wcharCount);
    if (sentinel_check(outputSize)) {
      diag_crash_msg("GetEnvironmentVariable() malformed output");
    }
    winutils_from_widestr(dynstring_push(output, outputSize), buffer.ptr, (usize)wcharCount);
  }

  alloc_free(g_allocScratch, buffer);
  return true;
}

void env_var_set(const String name, const String value) {
  // Convert the name to a null-terminated wide-char string.
  const usize nameBufferSize = winutils_to_widestr_size(name);
  if (UNLIKELY(sentinel_check(nameBufferSize))) {
    // Name contains invalid utf8.
    return;
  }
  if (UNLIKELY(nameBufferSize >= env_var_max_name_size)) {
    diag_assert_fail(
        "Environment variable name with length {} exceeds maximum of {}",
        fmt_int(nameBufferSize),
        fmt_int(env_var_max_name_size));
    return;
  }
  Mem nameBufferMem = mem_stack(nameBufferSize);
  winutils_to_widestr(nameBufferMem, name);

  const Mem valueBufferMem = winutils_to_widestr_scratch(value);

  if (UNLIKELY(!SetEnvironmentVariable(
          (const wchar_t*)nameBufferMem.ptr, (const wchar_t*)valueBufferMem.ptr))) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "SetEnvironmentVariable() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }
}

void env_var_clear(const String name) {
  // Convert the name to a null-terminated wide-char string.
  const usize nameBufferSize = winutils_to_widestr_size(name);
  if (UNLIKELY(sentinel_check(nameBufferSize))) {
    // Name contains invalid utf8.
    return;
  }
  if (UNLIKELY(nameBufferSize >= env_var_max_name_size)) {
    diag_assert_fail(
        "Environment variable name with length {} exceeds maximum of {}",
        fmt_int(nameBufferSize),
        fmt_int(env_var_max_name_size));
    return;
  }
  Mem nameBufferMem = mem_stack(nameBufferSize);
  winutils_to_widestr(nameBufferMem, name);

  if (UNLIKELY(!SetEnvironmentVariable((const wchar_t*)nameBufferMem.ptr, null /* value */))) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "SetEnvironmentVariable() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }
}
