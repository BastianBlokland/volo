#include "core_diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void diag_log(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stdout, format, args);
}

void diag_log_err(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
}

void diag_assert_fail(const DiagCallSite* callsite, const char* msg) {
  diag_log_err("Assertion failed: '%s' [file: %s line: %i]\n", msg, callsite->file, callsite->line);
  diag_crash();
}

void diag_crash() {
  VOLO_DEBUG_BREAK();
  exit(1);
}
