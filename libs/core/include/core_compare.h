#pragma once
#include "core_types.h"

/**
 * Determine the order between two values.
 * Returns -1 if a is less then b.
 * Returns 0 if a is equal to b.
 * Returns 1 if a is greater then b.
 */
typedef i8 (*Compare)(const void* a, const void* b);

#define COMPARE_DECLARE(_TYPE_)                                                                    \
  i8 compare_##_TYPE_(const void*, const void*);                                                   \
  i8 compare_##_TYPE_##_reverse(const void*, const void*);

/**
 * Basic comparison functions for primitive types.
 * For each type there is a 'compare_X' and 'compare_X_reverse' function.
 */

COMPARE_DECLARE(i8)
COMPARE_DECLARE(i16)
COMPARE_DECLARE(i32)
COMPARE_DECLARE(i64)
COMPARE_DECLARE(u8)
COMPARE_DECLARE(u16)
COMPARE_DECLARE(u32)
COMPARE_DECLARE(u64)
COMPARE_DECLARE(size_t)
COMPARE_DECLARE(float)
