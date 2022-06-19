#pragma once
#include "core_memory.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Non-owning view over memory containing characters.
 * Encoding is assumed to be utf8.
 * NOTE: NOT null-terminated.
 */
typedef Mem String;

/**
 * 32-bit hash of a string.
 *
 * String hashes are cheaper to pass around and compare to each-other then pointers to character
 * data on the heap. In general string hashes are not reversible, but the textual representation can
 * be stored in a StringTable to make it reversible.
 *
 * NOTE: This assumes each string that is used in the program hashes to a unique 32 bit value.
 * NOTE: Meant for short strings, preferably less then 128 bytes.
 */
typedef u32 StringHash;

typedef enum {
  StringMatchFlags_None       = 0,
  StringMatchFlags_IgnoreCase = 1 << 0,
} StringMatchFlags;

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
 * NOTE: _STRING_ is expanded multiple times, so care must be taken when providing complex
 * expressions.
 */
#define string_end(_STRING_) ((u8*)(_STRING_).ptr + (_STRING_).size)

/**
 * Retrieve a u8 pointer to the last character in the string.
 * NOTE: _STRING_ is expanded multiple times, so care must be taken when providing complex
 * expressions.
 * Pre-condition: string.size > 0
 */
#define string_last(_STRING_) ((u8*)(_STRING_).ptr + (_STRING_).size - 1)

/**
 * Allocate a new string that contains the contents of all the given strings.
 * NOTE: Has to be explicitly freed using 'string_free'.
 */
#define string_combine(_ALLOC_, ...)                                                               \
  string_combine_raw(                                                                              \
      (_ALLOC_), (const String[]){VA_ARGS_SKIP_FIRST(0, ##__VA_ARGS__, string_empty)})

/**
 * Create a StringHash from a character literal.
 */
#define string_hash_lit(_LIT_) string_hash(string_lit(_LIT_))

/**
 * Create a 32 bit hash of the given string.
 */
StringHash string_hash(String);

/**
 * Create a string from a null-terminated character pointer.
 */
String string_from_null_term(const char*);

/**
 * Duplicate the given string in memory allocated from the allocator.
 * NOTE: Has to be explicitly freed using 'string_free'.
 */
String string_dup(Allocator*, String);

/**
 * Allocate a new string that contains the contents of all the given strings.
 * NOTE: Has to be explicitly freed using 'string_free'.
 *
 * Pre-condition: 'parts' array should be terminated with an empty string (at least an pointer sized
 * section of 0 bytes).
 */
String string_combine_raw(Allocator*, const String* parts);

/**
 * Free previously allocated string.
 * Pre-condition: Given string was allocated from the same allocator.
 * Pre-condition: Given string has to be the full allocated string, not a substring.
 */
void string_free(Allocator*, String);
void string_maybe_free(Allocator*, String);

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
bool string_ends_with(String, String end);

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
 * NOTE: Returns 'sentinel_usize' if the substring could not be found.
 */
usize string_find_first(String, String subStr);

/**
 * Find the first occurrence of any of the given characters.
 * NOTE: Returns 'sentinel_usize' if none of the characters could be found.
 */
usize string_find_first_any(String, String chars);

/**
 * Find the last occurrence of the given substring.
 * NOTE: Returns 'sentinel_usize' if the substring could not be found.
 */
usize string_find_last(String, String subStr);

/**
 * Find the last occurrence of any of the given characters.
 * NOTE: Returns 'sentinel_usize' if none of the characters could be found.
 */
usize string_find_last_any(String, String chars);

/**
 * Match the given string to a glob pattern.
 *
 * Supported pattern syntax:
 * '?' matches any single character.
 * '*' matches any number of any characters including none.
 */
bool string_match_glob(String, String pattern, StringMatchFlags);

/**
 * Trim any characters contained in 'chars' from the beginning and ending of the string.
 */
String string_trim(String, String chars);

/**
 * Trim any whitespace from the beginning and ending of the string.
 */
String string_trim_whitespace(String);
