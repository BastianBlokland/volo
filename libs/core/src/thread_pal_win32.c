#include "core_diag.h"
#include "core_winutils.h"
#include "thread_internal.h"
#include <Windows.h>

i64 thread_pal_pid() { return GetCurrentProcessId(); }
i64 thread_pal_tid() { return GetCurrentThreadId(); }

void thread_pal_name_current(const String str) {
  static const usize maxNameLen = 15;
  if (str.size > maxNameLen) {
    diag_assert_fail(
        &diag_callsite_create(), string_lit("Thread name too long, maximum is 15 chars"));
  }

  const usize bufferSize = winutils_to_widestr_size(str);
  if (sentinel_check(bufferSize)) {
    diag_assert_fail(&diag_callsite_create(), string_lit("Thread name contains invalid utf8"));
  }
  Mem buffer = mem_stack(bufferSize);
  winutils_to_widestr(buffer, str);

  const HANDLE  curThread = GetCurrentThread();
  const HRESULT res       = SetThreadDescription(curThread, buffer.ptr);
  diag_assert_msg(SUCCEEDED(res), string_lit("SetThreadDescription() failed"));
  (void)res;
}
