#pragma once
#include "core_string.h"
#include "core_types.h"

/**
 * Function to validate input passed to a command-line option.
 */
typedef bool (*CliValidateFunc)(const String input);

/**
 * Validate if the given input is a valid i64 integer.
 */
bool cli_validate_i64(const String input);

/**
 * Validate if the given input is a valid u64 integer.
 */
bool cli_validate_u64(const String input);
