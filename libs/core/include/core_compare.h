#pragma once
#include "core_string.h"
#include "core_types.h"

/**
 * Determine the order between two values.
 * Returns -1 if a is less then b.
 * Returns 0 if a is equal to b.
 * Returns 1 if a is greater then b.
 */
typedef i8 (*CompareFunc)(const void* a, const void* b);

#define COMPARE_DECLARE_WITH_NAME(_TYPE_, _NAME_)                                                  \
  i8 compare_##_NAME_(const void*, const void*);                                                   \
  i8 compare_##_NAME_##_reverse(const void*, const void*);

#define COMPARE_DECLARE(_TYPE_) COMPARE_DECLARE_WITH_NAME(_TYPE_, _TYPE_)

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
COMPARE_DECLARE(usize)
COMPARE_DECLARE(f32)
COMPARE_DECLARE(f64)
COMPARE_DECLARE_WITH_NAME(StringHash, stringhash)
COMPARE_DECLARE_WITH_NAME(String, string)
