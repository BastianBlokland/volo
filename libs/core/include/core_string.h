#pragma once
#include "core_memory.h"

/**
 * Non-owning view over memory containing characters.
 * Note: NOT guaranteed to be null-terminated.
 */
typedef Mem String;

/**
 * Create an empty (0 characters) string.
 */
#define string_empty() string_lit("")

/**
 * Create a string over a character literal.
 */
#define string_lit(_LIT_)                                                                          \
  ((String){                                                                                       \
      .ptr  = (void*)(_LIT_),                                                                      \
      .size = sizeof(_LIT_) - 1u,                                                                  \
  })

/**
 * Compare strings a and b character wise.
 * Returns -1, 1 or 1.
 */
i8 string_cmp(String a, String b);

/**
 * Check if strings a and b are equal.
 */
bool string_eq(String a, String b);
