#pragma once
#include "core_memory.h"

/**
 * Returns the amount of elements in an array.
 */
#define array_elems(_ARRAY_) (sizeof(_ARRAY_) / sizeof((_ARRAY_)[0]))

/**
 * Creates a memory view over the given array.
 */
#define array_mem(_ARRAY_) mem_create((void*)(_ARRAY_), sizeof(_ARRAY_))

/**
 * Iterate of all values in the given array.
 * Pre-condition: sizeof(_TYPE_) has to match the element size of the array.
 */
#define array_for_t(_ARRAY_, _TYPE_, _VAR_, ...)                                                   \
  {                                                                                                \
    _TYPE_* _VAR_       = (_TYPE_*)(_ARRAY_);                                                      \
    _TYPE_* _VAR_##_end = (_TYPE_*)_VAR_ + array_elems(_ARRAY_);                                   \
    for (; _VAR_ != _VAR_##_end; ++_VAR_) {                                                        \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }
