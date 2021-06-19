#include "core_diag.h"
#include "core_shuffle.h"

void shuffle_fisheryates(Rng* rng, u8* begin, u8* end, const u16 stride) {
  /**
   * Basic Fisher–Yates shuffle.
   * More info: https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
   */
  for (usize n = ((end - begin) / stride) - 1; n > 1; --n) {
    usize k = rng_sample_range(rng, 0, n);
    mem_swap_raw(begin + n * stride, begin + k * stride, stride);
  }
}
