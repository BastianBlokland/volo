#pragma once
#include "core_types.h"

/**
 * Check if the given byte is a valid ascii character.
 */
bool ascii_is_valid(u8);

/**
 * Check if the given byte is a ascii digit.
 */
bool ascii_is_digit(u8);

/**
 * Check if the given byte is a ascii hexadecimal digit.
 */
bool ascii_is_hex_digit(u8);

/**
 * Check if the given byte is a ascii letter.
 */
bool ascii_is_letter(u8);

/**
 * Check if the given byte is a ascii lowercase letter.
 */
bool ascii_is_lower(u8);

/**
 * Check if the given byte is a ascii uppercase letter.
 */
bool ascii_is_upper(u8);

/**
 * Check if the given byte is a ascii control character.
 */
bool ascii_is_control(u8);

/**
 * Check if the given byte is a ascii whitespace character.
 */
bool ascii_is_whitespace(u8);

/**
 * Check if the given byte is a ascii newline character.
 */
bool ascii_is_newline(u8);

/**
 * Check if the given byte is a printable ascii character.
 */
bool ascii_is_printable(u8);

/**
 * Flip the casing of the given ascii letter.
 */
u8 ascii_toggle_case(u8);

/**
 * Convert the given ascii character to uppercase.
 */
u8 ascii_to_upper(u8);

/**
 * Convert the given ascii character to lowercase.
 */
u8 ascii_to_lower(u8);
