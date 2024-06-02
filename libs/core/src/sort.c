#include "core_array.h"
#include "core_diag.h"
#include "core_sort.h"

// #define VOLO_SORT_VERIFY

#define sort_quicksort_elems_min 10

INLINE_HINT static void sort_swap(u8* a, u8* b, u16 bytes) {
  do {
    const u8 tmp = *a;
    *a++         = *b;
    *b++         = tmp;
  } while (--bytes);
}

/**
 * Sort the given range using a basic insertion-sort scheme.
 * https://en.wikipedia.org/wiki/Insertion_sort
 */
static void sort_insert(u8* begin, u8* end, const u16 stride, CompareFunc compare) {
  for (u8* a = begin + stride; a < end; a += stride) {
    for (u8* b = a; b != begin && compare(b, b - stride) < 0; b -= stride) {
      sort_swap(b, b - stride, stride);
    }
  }
}

/**
 * Select a pivot to partition on using the median-of-three scheme.
 * NOTE: Makes sure the first and last elements are sorted with respect to the pivot.
 */
INLINE_HINT static u8* quicksort_pivot(u8* begin, u8* end, const u16 stride, CompareFunc compare) {
  const usize elems  = (end - begin) / stride;
  u8*         center = begin + elems / 2 * stride;
  if (compare(center, begin) < 0) {
    sort_swap(center, begin, stride);
  }
  if (compare(end - stride, center) < 0) {
    sort_swap(center, end - stride, stride);
  } else {
    return center;
  }
  if (compare(center, begin) < 0) {
    sort_swap(center, begin, stride);
  }
  return center;
}

/**
 * Partition the given range so that the elements before the returned partition point are less then
 * the partition-point and the elements after it are not-less.
 *
 * Hoare's partition scheme:
 * - https://en.wikipedia.org/wiki/Quicksort#Hoare_partition_scheme
 */
static u8* quicksort_partition(u8* begin, u8* end, const u16 stride, CompareFunc compare) {
  // Choose a pivot.
  u8* pivot = quicksort_pivot(begin, end, stride, compare);

  // First and last elements are already sorted by 'quicksort_pivot' so can be skipped.
  begin += stride;
  end -= stride;

  while (true) {

    // Skip over elements at the start that are correctly placed (less then the partition point).
    while (compare(begin, pivot) < 0) {
      begin += stride;
    }

    // Skip over elements at the end that are correctly placed (not less then the partition point).
    do {
      end -= stride;
    } while (compare(end, pivot) > 0);

    // If both ends meet then the partition is finished.
    if (begin >= end) {
      return begin;
    }

    // Begin is less then end, so swap them.
    sort_swap(begin, end, stride);

    // Patch up the pivot pointer in case it was moved.
    if (begin == pivot) {
      pivot = end;
    } else if (end == pivot) {
      pivot = begin;
    }

    begin += stride;
  }
}

typedef struct {
  u8* begin;
  u8* end;
} QuickSortSection;

void sort_quicksort(u8* begin, u8* end, const u16 stride, CompareFunc compare) {
  /**
   * Non-recursive QuickSort using Hoare's partition scheme.
   * - https://en.wikipedia.org/wiki/Quicksort
   */
  QuickSortSection stack[128];
  u32              stackSize = 0;

  stack[stackSize++] = (QuickSortSection){.begin = begin, .end = end};

  while (stackSize) {
    const QuickSortSection section = stack[--stackSize];

    if ((section.end - section.begin) < (stride * sort_quicksort_elems_min)) {
      // Small section; use insertion sort.
      sort_insert(section.begin, section.end, stride, compare);
      continue;
    }

    u8* partition      = quicksort_partition(section.begin, section.end, stride, compare);
    stack[stackSize++] = (QuickSortSection){.begin = section.begin, .end = partition};
    stack[stackSize++] = (QuickSortSection){.begin = partition, .end = section.end};
    diag_assert(stackSize < array_elems(stack));
  }

#ifdef VOLO_SORT_VERIFY
  for (u8* itr = begin + stride; itr < end; itr += stride) {
    u8* prev = itr - stride;
    diag_assert(compare(prev, itr) <= 0);
  }
#endif
}

void sort_bubblesort(u8* begin, u8* end, const u16 stride, CompareFunc compare) {
  /**
   * Basic BubbleSort implementation.
   * - https://en.wikipedia.org/wiki/Bubble_sort
   *
   * Not an efficient algorithm, but it useful for testing other sorting algorithms against.
   */

  usize len = (end - begin) / stride;
  while (len) {
    usize newLen = 0;
    for (usize i = 1; i != len; ++i) {
      u8* a = begin + (i - 1) * stride;
      u8* b = begin + i * stride;
      if (compare(a, b) > 0) {
        sort_swap(a, b, stride);
        newLen = i;
      }
    }
    len = newLen;
  }
}

/**
 * Select a pivot to partition on.
 * At the moment we always use the center element as the pivot.
 */
INLINE_HINT static usize index_quicksort_pivot(const usize begin, const usize end) {
  return begin + (end - begin) / 2;
}

/**
 * Partition the given range so that the elements before the returned partition point are less then
 * the partition-point and the elements after it are not-less.
 *
 * Hoare's partition scheme:
 * - https://en.wikipedia.org/wiki/Quicksort#Hoare_partition_scheme
 */
static usize index_quicksort_partition(
    void* ctx, usize begin, usize end, SortIndexCompare compare, SortIndexSwap swap) {
  // Choose a pivot.
  usize pivot = index_quicksort_pivot(begin, end);

  while (true) {

    // Skip over elements at the start that are correctly placed (less then the partition point).
    while (compare(ctx, begin, pivot) < 0) {
      ++begin;
    }

    // Skip over elements at the end that are correctly placed (not less then the partition point).
    do {
      --end;
    } while (compare(ctx, end, pivot) > 0);

    // If both ends meet then the partition is finished.
    if (begin >= end) {
      return begin;
    }

    // Begin is less then end, so swap them.
    swap(ctx, begin, end);

    // Patch up the pivot index in case it was moved.
    if (begin == pivot) {
      pivot = end;
    } else if (end == pivot) {
      pivot = begin;
    }

    ++begin;
  }
}

void sort_index_quicksort(
    void* ctx, const usize begin, const usize end, SortIndexCompare compare, SortIndexSwap swap) {
  if ((end - begin) < 2) {
    return; // Less then 2 items, nothing to do.
  }

  /**
   * Details on the algorithm:
   * - https://en.wikipedia.org/wiki/Quicksort
   */

  const usize partition = index_quicksort_partition(ctx, begin, end, compare, swap);
  sort_index_quicksort(ctx, begin, partition, compare, swap);
  sort_index_quicksort(ctx, partition, end, compare, swap);
}
