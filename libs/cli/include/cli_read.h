#pragma once
#include "core_types.h"

// Forward declare from 'cli_parse.h'.
typedef struct sCliInvocation CliInvocation;

// Forward declare from 'cli_app.h'.
typedef u16 CliId;

/**
 * Read a i64 integer that was provided to the given option.
 * If the option was not provided then 'defaultVal' is returned.
 */
i64 cli_read_i64(CliInvocation*, CliId, i64 defaultVal);

/**
 * Read a u64 integer that was provided to the given option.
 * If the option was not provided then 'defaultVal' is returned.
 */
u64 cli_read_u64(CliInvocation*, CliId, u64 defaultVal);
