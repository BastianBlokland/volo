#pragma once
#include "cli/forward.h"

typedef enum {
  CliHelpFlags_None           = 0,
  CliHelpFlags_Style          = 1 << 0,
  CliHelpFlags_IncludeVersion = 1 << 1,
} CliHelpFlags;

/**
 * Write a help page showing the available flags and arguments.
 */
void cli_help_write(DynString*, const CliApp*, CliHelpFlags);

/**
 * Write a help page showing the available flags and arguments to the given file.
 */
void cli_help_write_file(const CliApp*, CliHelpFlags, File* out);
