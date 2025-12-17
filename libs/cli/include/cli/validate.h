#pragma once
#include "core/forward.h"
#include "core/string.h"

/**
 * Function to validate input passed to a command-line option.
 */
typedef bool (*CliValidateFunc)(const String input);

/**
 * Validate if the given input is a valid signed integer.
 */
bool cli_validate_i64(const String input);

/**
 * Validate if the given input is a valid unsigned integer.
 */
bool cli_validate_u8(const String input);
bool cli_validate_u16(const String input);
bool cli_validate_u64(const String input);

/**
 * Validate if the given input is a valid f64 floating point number.
 */
bool cli_validate_f64(const String input);

/**
 * Validate file paths.
 */
bool cli_validate_file(const String input);
bool cli_validate_file_regular(const String input);
bool cli_validate_file_directory(const String input);
