#include "core_search.h"

void* search_linear(u8* begin, u8* end, const u16 stride, CompareFunc compare, const void* target) {
  /**
   * Linear scan to find the first matching element.
   */
  for (; begin < end; begin += stride) {
    if (compare(begin, target) == 0) {
      return begin;
    }
  }
  return null; // Not found.
}

void* search_binary(u8* begin, u8* end, const u16 stride, CompareFunc compare, const void* target) {
  /**
   * Binary scan to find the first matching element.
   */
  while (begin < end) {
    const usize elems  = (end - begin) / stride;
    u8*         middle = begin + (elems / 2) * stride;
    const i8    comp   = compare(middle, target);
    if (comp == 0) {
      return middle;
    }
    if (comp > 0) {
      end = middle; // Disregard everything after (and including) middle.
    } else {
      begin = middle + stride; // Discard everything before (and including) middle.
    }
  }
  return null; // Not found.
}
