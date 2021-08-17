#pragma once
#include "core_dynstring.h"

// Forward declare from 'core_file.h'.
typedef struct sFile File;

// Forward declare from 'cli_app.h'.
typedef struct sCliApp CliApp;

// Forward declare from 'core_parse.h'.
typedef struct sCliInvocation CliInvocation;

typedef enum {
  CliFailureFlags_None  = 0,
  CliFailureFlags_Style = 1 << 0,
} CliFailureFlags;

/**
 * TODO.
 */
void cli_failure_write(DynString*, CliInvocation*, CliFailureFlags);

/**
 * TODO.
 */
void cli_failure_write_file(CliInvocation*, File* out);
