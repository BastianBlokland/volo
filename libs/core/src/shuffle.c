#include "core_rng.h"
#include "core_shuffle.h"

INLINE_HINT static void shuffle_swap(u8* a, u8* b, u16 bytes) {
  do {
    const u8 tmp = *a;
    *a++         = *b;
    *b++         = tmp;
  } while (--bytes);
}

void shuffle_fisheryates(Rng* rng, u8* begin, u8* end, const u16 stride) {
  /**
   * Basic Fisher–Yates shuffle.
   * More info: https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
   */
  for (usize n = ((end - begin) / stride) - 1; n > 1; --n) {
    const usize k = (usize)rng_sample_range(rng, 0, n);
    shuffle_swap(begin + n * stride, begin + k * stride, stride);
  }
}
