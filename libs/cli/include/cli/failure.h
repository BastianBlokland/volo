#pragma once
#include "cli/forward.h"

typedef enum {
  CliFailureFlags_None  = 0,
  CliFailureFlags_Style = 1 << 0,
} CliFailureFlags;

/**
 * Write a failure page showing the invocation errors.
 */
void cli_failure_write(DynString*, CliInvocation*, CliFailureFlags);

/**
 * Write a failure page showing the invocation errors to the given file.
 */
void cli_failure_write_file(CliInvocation*, File* out);
