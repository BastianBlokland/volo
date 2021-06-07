#include "core_array.h"
#include "core_diag.h"
#include "core_sort.h"

void sort_bubblesort(u8* begin, u8* end, u16 stride, CompareFunc compare) {
  /**
   * Basic BubbleSort implementation.
   * - https://en.wikipedia.org/wiki/Bubble_sort
   *
   * Not an effiecient algorithm, but it usefull for testing other sorting algorithms against.
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
