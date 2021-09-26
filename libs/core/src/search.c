#include "core_search.h"

void* search_linear(
    u8* begin, u8* end, const u16 stride, const CompareFunc compare, const void* target) {
  /**
   * Linear scan for the first matching element.
   */
  for (u8* itr = begin; itr < end; itr += stride) {
    if (compare(itr, target) == 0) {
      return itr;
    }
  }
  return null;
}
