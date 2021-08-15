#pragma once
#include "core_types.h"

// Forward declare from 'cli_parse.h'.
typedef struct sCliInvocation CliInvocation;

// Forward declare from 'cli_app.h'.
typedef u16 CliId;

/**
 * TODO.
 */
i64 cli_read_i64(CliInvocation*, CliId, i64 defaultVal);

/**
 * TODO.
 */
u64 cli_read_u64(CliInvocation*, CliId, u64 defaultVal);
