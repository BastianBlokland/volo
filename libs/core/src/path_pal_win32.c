#include "core_diag.h"
#include "core_env.h"
#include "core_sentinel.h"
#include "core_winutils.h"

#include <Windows.h>

#include "path_internal.h"

static String path_canonize_to_output_buffer(Mem outputBuffer, String path) {
  DynString writer = dynstring_create_over(outputBuffer);
  path_canonize(&writer, path);

  String result = dynstring_view(&writer);
  dynstring_destroy(&writer);
  return result;
}

String path_pal_workingdir(Mem outputBuffer) {
  // Retrieve the working directory from win32 into a wide-char buffer on the stack.
  Mem wideTmp = mem_stack((path_pal_max_size + 1) * sizeof(wchar_t)); // +1 for null-terminator.
  const usize wideTmpSize = GetCurrentDirectory(path_pal_max_size, wideTmp.ptr);
  if (!wideTmpSize || wideTmpSize >= path_pal_max_size) {
    diag_crash_msg("GetCurrentDirectory() failed");
  }

  // Convert the wide-char path into utf8.
  const usize utf8TmpSize = winutils_from_widestr_size(wideTmp.ptr, wideTmpSize);
  if (sentinel_check(utf8TmpSize)) {
    diag_crash_msg("GetCurrentDirectory() malformed output");
  }
  Mem utf8Tmp = mem_stack(utf8TmpSize);
  winutils_from_widestr(utf8Tmp, wideTmp.ptr, wideTmpSize);

  return path_canonize_to_output_buffer(outputBuffer, utf8Tmp);
}

String path_pal_executable(Mem outputBuffer) {
  // Retrieve the executable path from win32 into a wide-char buffer on the stack.
  Mem wideTmp = mem_stack((path_pal_max_size + 1) * sizeof(wchar_t)); // +1 for null-terminator.
  const usize wideTmpSize = GetModuleFileName(null, wideTmp.ptr, path_pal_max_size);
  if (!wideTmpSize || wideTmpSize >= path_pal_max_size) {
    diag_crash_msg("GetModuleFileName() failed");
  }

  // Convert the wide-char path into utf8.
  const usize utf8TmpSize = winutils_from_widestr_size(wideTmp.ptr, wideTmpSize);
  if (sentinel_check(utf8TmpSize)) {
    diag_crash_msg("GetModuleFileName() malformed output");
  }
  Mem utf8Tmp = mem_stack(utf8TmpSize);
  winutils_from_widestr(utf8Tmp, wideTmp.ptr, wideTmpSize);

  return path_canonize_to_output_buffer(outputBuffer, utf8Tmp);
}

String path_pal_tempdir(Mem outputBuffer) {
  DynString tmpWriter = dynstring_create_over(mem_stack(path_pal_max_size));
  String    result;

  if (env_var(string_lit("TMPDIR"), &tmpWriter)) {
    result = path_canonize_to_output_buffer(outputBuffer, dynstring_view(&tmpWriter));
    goto Ret;
  }

  if (env_var(string_lit("TEMP"), &tmpWriter)) {
    result = path_canonize_to_output_buffer(outputBuffer, dynstring_view(&tmpWriter));
    goto Ret;
  }

  if (env_var(string_lit("TMP"), &tmpWriter)) {
    result = path_canonize_to_output_buffer(outputBuffer, dynstring_view(&tmpWriter));
    goto Ret;
  }

  diag_crash_msg("System temp directory could not be found");

Ret:
  dynstring_destroy(&tmpWriter);
  return result;
}
