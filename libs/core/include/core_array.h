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

// clang-format off

/**
 * Iterate over all values in the given array.
 * Pre-condition: sizeof(_TYPE_) has to match the element size of the array.
 */
#define array_for_t(_ARRAY_, _TYPE_, _VAR_)                                                        \
  for (_TYPE_* _VAR_      = (_TYPE_*)(_ARRAY_),                                                    \
             *_VAR_##_end = (_TYPE_*)_VAR_ + array_elems(_ARRAY_);                                 \
       _VAR_ != _VAR_##_end;                                                                       \
       ++_VAR_)

/**
 * Iterate over all values in an array defined by a pointer named 'values' and a count.
 *
 * Example struct:
 * '
 *  struct MyArray {
 *    i32*  values;
 *    usize count;
 *  };
 * '
 *
 * NOTE: _ARRAY_ is expanded twice, so care must be taken when providing complex expressions.
 * Pre-condition: sizeof(_TYPE_) has to match the element size of the 'values' pointer.
 */
#define array_ptr_for_t(_ARRAY_STRUCT_, _TYPE_, _VAR_)                                             \
  for (_TYPE_* _VAR_      = (_TYPE_*)((_ARRAY_STRUCT_).values),                                    \
             *_VAR_##_end = (_TYPE_*)_VAR_ + (_ARRAY_STRUCT_).count;                               \
       _VAR_ != _VAR_##_end;                                                                       \
       ++_VAR_)

// clang-format on
