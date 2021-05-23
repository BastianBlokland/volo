#include "core_array.h"
#include "core_diag.h"
#include "core_sort.h"

static void quicksort_swap(void* a, void* b, u16 stride) {
  u8  buffer[stride];
  Mem memTemp = array_mem(buffer);
  Mem memA    = mem_create(a, stride);
  Mem memB    = mem_create(b, stride);

  mem_cpy(memTemp, memA);
  mem_cpy(memA, memB);
  mem_cpy(memB, memTemp);
}

/**
 * Select a pivot to partition on.
 * At the moment we always use the center element as the pivot.
 */
static void* quicksort_pivot(void* begin, void* end, u16 stride) {
  const usize diffBytes = (u8*)end - (u8*)begin;
  const usize diffElems = diffBytes / stride;
  return begin + diffElems / 2 * stride;
}

/**
 * Partition the given range so that the elements before the returned partition point are less then
 * the partition-point and the elements after it are not-less.
 *
 * Hoare's partition scheme:
 * - https://en.wikipedia.org/wiki/Quicksort#Hoare_partition_scheme
 */
static void* quicksort_partition(void* begin, void* end, u16 stride, Compare compare) {
  void* pivot = quicksort_pivot(begin, end, stride);
  while (true) {

    // Skip over elements at the start that are correctly placed (less then the partition point).
    while (compare(begin, pivot) < 0) {
      begin += stride;
    }

    // Skip over elements at the end that are correctly placed (not less then the partition point).
    do {
      end -= stride;
    } while (compare(pivot, end) < 0);

    // If both ends meet then the partition is finished.
    if (begin >= end) {
      return begin;
    }

    quicksort_swap(begin, end, stride);
    begin += stride;
  }
}

void quicksort(void* begin, void* end, u16 stride, Compare compare) {
  // Guard against excessive stack use, reason is we create a temporary buffer on the stack of this
  // size during swapping of elements.
  diag_assert(stride <= 128);

  if (end - begin <= stride) {
    return; // Less then 2 items, nothing to do.
  }

  /**
   * Details on the algorithm:
   * - https://en.wikipedia.org/wiki/Quicksort
   */

  void* partition = quicksort_partition(begin, end, stride, compare);
  quicksort(begin, partition, stride, compare);
  quicksort(partition, end, stride, compare);
}
