#pragma once
#include "core_types.h"
#include "snd_channel.h"

typedef struct {
  f32 samples[SndChannel_Count];
} SndBufferFrame;

typedef struct {
  SndBufferFrame* frames;
  usize           frameCount;
} SndBuffer;

typedef struct {
  const SndBufferFrame* frames;
  usize                 frameCount;
} SndBufferView;

/**
 * Sample the buffer at the given fraction.
 * Pre-condition: frac >= 0.0 && frac <= 1.0.
 * Pre-condition: view.frameCount >= 2.
 */
f32 snd_buffer_sample(SndBufferView, SndChannel, f32 frac);

/**
 * Compute the peak level (amplitude) of the sound.
 */
f32 snd_buffer_level_peak(SndBufferView, SndChannel);

/**
 * Compute the RMS (aka quadratic mean) level (amplitude) of the sound.
 * More info: https://en.wikipedia.org/wiki/Root_mean_square
 */
f32 snd_buffer_level_rms(SndBufferView, SndChannel);
