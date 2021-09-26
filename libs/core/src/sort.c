#include "core_array.h"
#include "core_diag.h"
#include "core_sort.h"

/**
 * Select a pivot to partition on.
 * At the moment we always use the center element as the pivot.
 */
static Mem quicksort_pivot(u8* begin, u8* end, u16 stride) {
  const usize elems = (end - begin) / stride;
  return mem_create(begin + elems / 2 * stride, stride);
}

/**
 * Partition the given range so that the elements before the returned partition point are less then
 * the partition-point and the elements after it are not-less.
 *
 * Hoare's partition scheme:
 * - https://en.wikipedia.org/wiki/Quicksort#Hoare_partition_scheme
 */
static u8* quicksort_partition(u8* begin, u8* end, u16 stride, CompareFunc compare) {
  // Choose a pivot.
  // Note: Make a copy of the pivot, needed because it might move due to the swapping.
  Mem pivot = mem_stack(stride);
  mem_cpy(pivot, quicksort_pivot(begin, end, stride));

  while (true) {

    // Skip over elements at the start that are correctly placed (less then the partition point).
    while (compare(begin, pivot.ptr) < 0) {
      begin += stride;
    }

    // Skip over elements at the end that are correctly placed (not less then the partition point).
    do {
      end -= stride;
    } while (compare(end, pivot.ptr) > 0);

    // If both ends meet then the partition is finished.
    if (begin >= end) {
      return begin;
    }

    // Begin is less then end, so swap them.
    mem_swap_raw(begin, end, stride);

    begin += stride;
  }
}

void sort_quicksort(u8* begin, u8* end, u16 stride, CompareFunc compare) {
  if ((end - begin) < (stride * 2)) {
    return; // Less then 2 items, nothing to do.
  }

  /**
   * Details on the algorithm:
   * - https://en.wikipedia.org/wiki/Quicksort
   */

  void* partition = quicksort_partition(begin, end, stride, compare);
  sort_quicksort(begin, partition, stride, compare);
  sort_quicksort(partition, end, stride, compare);
}

void sort_bubblesort(u8* begin, u8* end, u16 stride, CompareFunc compare) {
  /**
   * Basic BubbleSort implementation.
   * - https://en.wikipedia.org/wiki/Bubble_sort
   *
   * Not an efficient algorithm, but it usefull for testing other sorting algorithms against.
   */

  usize len = (end - begin) / stride;
  while (len) {
    usize newLen = 0;
    for (usize i = 1; i != len; ++i) {
      u8* a = begin + (i - 1) * stride;
      u8* b = begin + i * stride;
      if (compare(a, b) > 0) {
        mem_swap_raw(a, b, stride);
        newLen = i;
      }
    }
    len = newLen;
  }
}
