#pragma once
#include "core_memory.h"

/**
 * Non-owning view over memory containing characters.
 * Note: NOT guaranteed to be null-terminated.
 */
typedef Mem String;

#define string_from_lit(_LIT_)                                                                     \
  ((String){                                                                                       \
      .ptr  = (void*)(_LIT_),                                                                      \
      .size = sizeof(_LIT_) - 1u,                                                                  \
  })

i32  string_cmp(String a, String b);
bool string_eq(String a, String b);
