#pragma once
#include "core_compare.h"

/**
 * Search for an element matching the given target using a linear-scan.
 * NOTE: Data is not required to be sorted.
 *
 * Example usage:
 * ```
 * i32  ints[] = {1, 42, 2, 8, 49, 3};
 * i32  target = 42;
 * i32* found  = search_linear_t(ints, ints + array_elems(ints), i32, compare_i32, &target);
 * ```
 */
#define search_linear_t(_BEGIN_, _END_, _TYPE_, _COMPARE_FUNC_, _TARGET_)                          \
  ((_TYPE_*)search_linear(                                                                         \
      (u8*)(_BEGIN_), (u8*)(_END_), sizeof(_TYPE_), (_COMPARE_FUNC_), (_TARGET_)))

/**
 * Search for an element matching a struct using a linear-scan.
 * NOTE: Data is not required to be sorted.
 *
 * Example usage:
 * ```
 * MyStruct  data[] = {{.key = 9, .value = 1}, {.key = 42, .value = 2}};
 * MyStruct* found  = search_linear_struct_t(
 *     data, data + array_elems(data), MyStruct, compare_mystruct, .key = 42);
 * ```
 */
#define search_linear_struct_t(_BEGIN_, _END_, _TYPE_, _COMPARE_FUNC_, ...)                        \
  search_linear_t((_BEGIN_), (_END_), _TYPE_, (_COMPARE_FUNC_), mem_struct(_TYPE_, __VA_ARGS__).ptr)

/**
 * Search for an element matching the given target using a linear-scan.
 * NOTE: Data is not required to be sorted.
 */
void* search_linear(u8* begin, u8* end, u16 stride, CompareFunc, const void* target);
