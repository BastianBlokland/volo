#include "core_diag.h"
#include "core_math.h"
#include "snd_buffer.h"

f32 snd_buffer_sample(const SndBufferView view, const SndChannel channel, const f32 frac) {
  diag_assert(frac >= 0.0 && frac <= 1.0f);
  diag_assert(view.frameCount >= 2);

  /**
   * Linear interpolation between the two closest samples.
   * NOTE: We can explore other methods that preserve the curve better, like Hermite interpolation.
   */

  const f32 index = frac * (view.frameCount - 1);
  const f32 edgeA = math_min(view.frameCount - 2, math_round_down_f32(index));
  const f32 edgeB = edgeA + 1.0f;
  const f32 valA  = view.frames[(usize)edgeA].samples[channel];
  const f32 valB  = view.frames[(usize)edgeB].samples[channel];
  return math_lerp(valA, valB, index - edgeA);
}
