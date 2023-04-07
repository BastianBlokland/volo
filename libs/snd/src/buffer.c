#include "core_diag.h"
#include "core_math.h"
#include "snd_buffer.h"

SndBufferView snd_buffer_view(const SndBuffer buffer) {
  return (SndBufferView){
      .frames     = buffer.frames,
      .frameCount = buffer.frameCount,
      .frameRate  = buffer.frameRate,
  };
}

SndBufferView snd_buffer_slice(const SndBufferView view, const u32 offset, const u32 count) {
  diag_assert(view.frameCount >= offset + count);
  return (SndBufferView){
      .frames     = view.frames + offset,
      .frameCount = count,
      .frameRate  = view.frameRate,
  };
}

TimeDuration snd_buffer_duration(const SndBufferView view) {
  return view.frameCount * time_second / view.frameRate;
}

f32 snd_buffer_frequency_max(const SndBufferView view) {
  /**
   * https://en.wikipedia.org/wiki/Nyquist_frequency
   */
  return view.frameRate * 0.5f;
}

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

f32 snd_buffer_level_peak(const SndBufferView view, const SndChannel channel) {
  f32 peak = 0;
  for (u32 frame = 0; frame != view.frameCount; ++frame) {
    const f32 sample    = view.frames[frame].samples[channel];
    const f32 sampleAbs = math_abs(sample);
    if (sampleAbs > peak) {
      peak = sampleAbs;
    }
  }
  return peak;
}

f32 snd_buffer_level_rms(const SndBufferView view, const SndChannel channel) {
  f32 sum = 0;
  for (u32 frame = 0; frame != view.frameCount; ++frame) {
    const f32 sample = view.frames[frame].samples[channel];
    sum += sample * sample;
  }
  sum /= view.frameCount;
  return math_sqrt_f32(sum);
}
