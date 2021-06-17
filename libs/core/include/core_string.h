#pragma once
#include "core_memory.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Non-owning view over memory containing characters.
 * Encoding is assumed to be utf8.
 * Note: NOT null-terminated.
 */
typedef Mem String;

/**
 * Create an empty (0 characters) string.
 */
#define string_empty ((String){0})

/**
 * Check if a string is empty (has 0 characters).
 */
#define string_is_empty(_STRING_) ((_STRING_).size == 0)

/**
 * Create a string over a character literal.
 */
#define string_static(_LIT_)                                                                       \
  { .ptr = (void*)(_LIT_), .size = sizeof(_LIT_) - 1u, }

/**
 * Create a string over a character literal.
 */
#define string_lit(_LIT_) ((String)string_static(_LIT_))

/**
 * Retrieve a u8 pointer to a specific character.
 * Pre-condition: '_IDX_' < string.size
 */
#define string_at(_STRING_, _IDX_) ((u8*)(_STRING_).ptr + (_IDX_))

/**
 * Retrieve a u8 pointer to the start of the string.
 */
#define string_begin(_STRING_) ((u8*)(_STRING_).ptr)

/**
 * Retrieve a u8 pointer to the end of the string (1 past the last valid character).
 */
#define string_end(_STRING_) ((u8*)(_STRING_).ptr + (_STRING_).size)

/**
 * Retrieve a u8 pointer to the last character in the string.
 * Pre-condition: string.size > 0
 */
#define string_last(_STRING_) ((u8*)(_STRING_).ptr + (_STRING_).size - 1)

/**
 * Create a string from a null-terminated character pointer.
 */
String string_from_null_term(const char*);

/**
 * Duplicate the given string in memory allocated from the allocator.
 * Note: Has to be explicitly freed using 'string_free'.
 */
String string_dup(Allocator*, String);

/**
 * Free previously allocated string.
 * Pre-condition: Given string was allocated from the same allocator.
 * Pre-condition: Given string has to be the full allocated string, not a substring.
 */
void string_free(Allocator*, String);

/**
 * Compare strings a and b character wise.
 * Returns -1, 1 or 1.
 */
i8 string_cmp(String a, String b);

/**
 * Check if strings a and b are equal.
 */
bool string_eq(String a, String b);

/**
 * Check if a string starts with a specific sub-string.
 */
bool string_starts_with(String, String start);

/**
 * Check if a string ends with a specific sub-string.
 */
bool string_ends_with(String, String start);

/**
 * Create a view to a sub-section of this string.
 * Pre-condition: string.size >= offset + size
 */
String string_slice(String, usize offset, usize size);

/**
 * Create a view 'amount' characters into the string.
 * Pre-condition: string.size >= amount.
 */
String string_consume(String, usize amount);

/**
 * Find the first occurrence of the given substring.
 * Note: Returns 'sentinel_usize' if the substring could not be found.
 */
usize string_find_first(String, String subStr);

/**
 * Find the first occurrence of any of the given characters.
 * Note: Returns 'sentinel_usize' if none of the characters could be found.
 */
usize string_find_first_any(String, String chars);

/**
 * Find the last occurrence of the given substring.
 * Note: Returns 'sentinel_usize' if the substring could not be found.
 */
usize string_find_last(String, String subStr);

/**
 * Find the last occurrence of any of the given characters.
 * Note: Returns 'sentinel_usize' if none of the characters could be found.
 */
usize string_find_last_any(String, String chars);
