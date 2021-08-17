#pragma once
#include "core_dynstring.h"

// Forward declare from 'core_file.h'.
typedef struct sFile File;

// Forward declare from 'cli_app.h'.
typedef struct sCliApp CliApp;

typedef enum {
  CliHelpFlags_None  = 0,
  CliHelpFlags_Style = 1 << 0,
} CliHelpFlags;

/**
 * Write a help page showing the available flags and arguments.
 */
void cli_help_write(DynString*, CliApp*, CliHelpFlags);

/**
 * Write a help page showing the available flags and arguments to the given file.
 */
void cli_help_write_file(CliApp*, File* out);
