#include "core_diag.h"
#include "core_winutils.h"
#include "thread_internal.h"
#include <Windows.h>

i64 thread_pal_pid() { return GetCurrentProcessId(); }
i64 thread_pal_tid() { return GetCurrentThreadId(); }

void thread_pal_set_name(const String str) {
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

ThreadHandle thread_pal_start(thread_pal_rettype (*routine)(void*), void* data) {
  HANDLE handle = CreateThread(null, thread_pal_stacksize, routine, data, 0, null);
  diag_assert_msg(handle, string_lit("CreateThread() failed"));

  _Static_assert(sizeof(ThreadHandle) >= sizeof(HANDLE), "'HANDLE' type too big");
  return (ThreadHandle)handle;
}

void thread_pal_join(ThreadHandle thread) {
  DWORD waitRes = WaitForSingleObject((HANDLE)thread, INFINITE);
  diag_assert_msg(waitRes != WAIT_FAILED, string_lit("WaitForSingleObject() failed"));
  (void)waitRes;

  BOOL closeRes = CloseHandle((HANDLE)thread);
  diag_assert_msg(closeRes, string_lit("CloseHandle() failed"));
  (void)closeRes;
}
