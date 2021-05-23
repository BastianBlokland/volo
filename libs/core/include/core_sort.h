#pragma once
#include "core_compare.h"
#include "core_types.h"

/**
 * Sort elements according to the given compare function.
 * Note: The sort is non-stable, meaning order of equal elements is NOT preserved.
 * Pre-condition: stride <= 128.
 */
void quicksort(void* begin, void* end, u16 stride, Compare);
