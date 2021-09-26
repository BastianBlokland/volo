#pragma once
#include "core_compare.h"

/**
 * Returns an element matching the given target or null if none matched.
 */
#define search_linear_t(_BEGIN_, _END_, _TYPE_, _COMPARE_FUNC_, _TARGET_)                          \
  ((_TYPE_*)search_linear(                                                                         \
      (u8*)(_BEGIN_), (u8*)(_END_), sizeof(_TYPE_), (_COMPARE_FUNC_), (_TARGET_)))

/**
 * Returns an element matching the given target or null if none matched.
 */
void* search_linear(u8* begin, u8* end, u16 stride, CompareFunc, const void* target);

/**
 * Returns an element matching the given target or null if none matched.
 * Pre-condition: data is sorted.
 */
#define search_binary_t(_BEGIN_, _END_, _TYPE_, _COMPARE_FUNC_, _TARGET_)                          \
  ((_TYPE_*)search_binary(                                                                         \
      (u8*)(_BEGIN_), (u8*)(_END_), sizeof(_TYPE_), (_COMPARE_FUNC_), (_TARGET_)))

/**
 * Returns an element matching the given target or null if none matched.
 * Pre-condition: data is sorted.
 */
void* search_binary(u8* begin, u8* end, u16 stride, CompareFunc, const void* target);

/**
 * Returns the first element greater then the given target (or null if none was greater).
 * Pre-condition: data is sorted.
 */
#define search_binary_greater_t(_BEGIN_, _END_, _TYPE_, _COMPARE_FUNC_, _TARGET_)                  \
  ((_TYPE_*)search_binary_greater(                                                                 \
      (u8*)(_BEGIN_), (u8*)(_END_), sizeof(_TYPE_), (_COMPARE_FUNC_), (_TARGET_)))

/**
 * Returns the first element greater then the given target (or null if none was greater).
 * Pre-condition: data is sorted.
 */
void* search_binary_greater(u8* begin, u8* end, u16 stride, CompareFunc, const void* target);
