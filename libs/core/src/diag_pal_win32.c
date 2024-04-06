#include "diag_internal.h"

#include <Windows.h>

#define diag_crash_exit_code 1

void diag_pal_break() {
  if (IsDebuggerPresent()) {
    DebugBreak();
  }
}

void diag_pal_crash() {
  HANDLE curProcess = GetCurrentProcess();
  TerminateProcess(curProcess, diag_crash_exit_code);
  UNREACHABLE
}
