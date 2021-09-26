#pragma once
#include "core_compare.h"

/**
 * Search for an element matching the given target using a linear-scan.
 */
#define search_linear_t(_BEGIN_, _END_, _TYPE_, _COMPARE_FUNC_, _TARGET_)                          \
  ((_TYPE_*)search_linear(                                                                         \
      (u8*)(_BEGIN_), (u8*)(_END_), sizeof(_TYPE_), (_COMPARE_FUNC_), (_TARGET_)))

/**
 * Search for an element matching a struct using a linear-scan.
 */
#define search_linear_struct_t(_BEGIN_, _END_, _TYPE_, _COMPARE_FUNC_, ...)                        \
  search_linear_t((_BEGIN_), (_END_), _TYPE_, (_COMPARE_FUNC_), mem_struct(_TYPE_, __VA_ARGS__).ptr)

/**
 * Search for an element matching the given target using a linear-scan.
 */
void* search_linear(u8* begin, u8* end, u16 stride, CompareFunc, const void* target);

/**
 * Search for an element matching the given target in sorted data using a binary scan.
 * Pre-condition: data is sorted.
 */
#define search_binary_t(_BEGIN_, _END_, _TYPE_, _COMPARE_FUNC_, _TARGET_)                          \
  ((_TYPE_*)search_binary(                                                                         \
      (u8*)(_BEGIN_), (u8*)(_END_), sizeof(_TYPE_), (_COMPARE_FUNC_), (_TARGET_)))

/**
 * Search for an element matching the given target in sorted data using a binary scan.
 * Pre-condition: data is sorted.
 */
#define search_binary_struct_t(_BEGIN_, _END_, _TYPE_, _COMPARE_FUNC_, ...)                        \
  search_binary_t((_BEGIN_), (_END_), _TYPE_, (_COMPARE_FUNC_), mem_struct(_TYPE_, __VA_ARGS__).ptr)

/**
 * Search for an element matching the given target in ordered data using a binary scan.
 * Pre-condition: data is sorted.
 */
void* search_binary(u8* begin, u8* end, u16 stride, CompareFunc, const void* target);
