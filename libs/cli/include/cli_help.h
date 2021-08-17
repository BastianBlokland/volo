#pragma once
#include "core_dynstring.h"

// Forward declare from 'cli_app.h'.
typedef struct sCliApp CliApp;

typedef enum {
  CliHelpFlags_None  = 0,
  CliHelpFlags_Style = 1 << 0,
} CliHelpFlags;

/**
 * TODO.
 */
void cli_write_help(DynString*, CliApp*, CliHelpFlags);
