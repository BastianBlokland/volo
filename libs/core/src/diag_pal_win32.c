#include "diag_internal.h"

#include <Windows.h>

void diag_pal_break() {
  if (IsDebuggerPresent()) {
    DebugBreak();
  }
}

void diag_pal_crash() { ExitProcess(1); }
