#pragma once
#include "core_string.h"
#include "core_types.h"

/**
 * TODO.
 */
typedef bool (*CliValidateFunc)(const String input);

/**
 * TODO.
 */
bool cli_validate_i64(const String input);

/**
 * TODO.
 */
bool cli_validate_u64(const String input);
