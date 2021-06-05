#pragma once
#include "core_memory.h"

/**
 * Non-owning view over memory containing characters.
 * Encoding is assumed to be utf8.
 * Note: NOT null-terminated.
 */
typedef Mem String;

/**
 * Create an empty (0 characters) string.
 */
#define string_empty() string_lit("")

/**
 * Check if a string is empty (has 0 characters).
 */
#define string_is_empty(_STRING_) ((_STRING_).size == 0)

/**
 * Create a string over a character literal.
 */
#define string_lit(_LIT_)                                                                          \
  ((String){                                                                                       \
      .ptr  = (void*)(_LIT_),                                                                      \
      .size = sizeof(_LIT_) - 1u,                                                                  \
  })

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
 * Compare strings a and b character wise.
 * Returns -1, 1 or 1.
 */
i8 string_cmp(String a, String b);

/**
 * Check if strings a and b are equal.
 */
bool string_eq(String a, String b);

/**
 * Create a view to a sub-section of this string.
 * Pre-condition: string.size >= offset + size
 */
String string_slice(String, usize offset, usize size);
