#pragma once
#include "core_compare.h"
#include "core_types.h"

typedef void (*SortFunc)(u8* begin, u8* end, u16 stride, CompareFunc);

/**
 * Sort elements according to the given compare function.
 * Note: The sort is non-stable, meaning order of equal elements is NOT preserved.
 * Pre-condition: stride <= 128.
 */
void sort_quicksort(u8* begin, u8* end, u16 stride, CompareFunc);

/**
 * Sort elements according to the given compare function.
 * Note: The sort is stable, meaning order of equal elements is preserved.
 * Pre-condition: stride <= 128.
 */
void sort_bubblesort(u8* begin, u8* end, u16 stride, CompareFunc);
