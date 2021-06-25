#include "core_diag.h"
#include "core_sentinel.h"
#include "core_winutils.h"

#include <Windows.h>

#include "path_internal.h"

String path_pal_workingdir(Mem outputBuffer) {
  // Retrieve the working directory from win32 into a utf16 buffer on the stack.
  Mem utf16Tmp = mem_stack(path_pal_max_size * sizeof(wchar_t) + 1); // +1 for null-terminator.
  const usize utf16TmpSize = GetCurrentDirectory(path_pal_max_size, utf16Tmp.ptr);
  if (!utf16TmpSize || utf16TmpSize >= path_pal_max_size) {
    diag_assert_fail("GetCurrentDirectory() failed");
  }

  // Convert the utf16 path into utf8.
  const usize utf8TmpSize = winutils_from_widestr_size(utf16Tmp.ptr, utf16TmpSize);
  if (sentinel_check(utf8TmpSize)) {
    diag_assert_fail("GetCurrentDirectory() malformed output");
  }
  Mem utf8Tmp = mem_stack(utf8TmpSize);
  winutils_from_widestr(utf8Tmp, utf16Tmp.ptr, utf16TmpSize);

  // Canonize the result and write it to the output buffer.
  DynString writer = dynstring_create_over(outputBuffer);
  path_canonize(&writer, utf8Tmp);

  String result = dynstring_view(&writer);
  dynstring_destroy(&writer);
  return result;
}

String path_pal_executable(Mem outputBuffer) {
  // Retrieve the executable path from win32 into a utf16 buffer on the stack.
  Mem utf16Tmp = mem_stack(path_pal_max_size * sizeof(wchar_t) + 1); // +1 for null-terminator.
  const usize utf16TmpSize = GetModuleFileName(null, utf16Tmp.ptr, path_pal_max_size);
  if (!utf16TmpSize || utf16TmpSize >= path_pal_max_size) {
    diag_assert_fail("GetModuleFileName() failed");
  }

  // Convert the utf16 path into utf8.
  const usize utf8TmpSize = winutils_from_widestr_size(utf16Tmp.ptr, utf16TmpSize);
  if (sentinel_check(utf8TmpSize)) {
    diag_assert_fail("GetModuleFileName() malformed output");
  }
  Mem utf8Tmp = mem_stack(utf8TmpSize);
  winutils_from_widestr(utf8Tmp, utf16Tmp.ptr, utf16TmpSize);

  // Canonize the result and write it to the output buffer.
  DynString writer = dynstring_create_over(outputBuffer);
  path_canonize(&writer, utf8Tmp);

  String result = dynstring_view(&writer);
  dynstring_destroy(&writer);
  return result;
}
