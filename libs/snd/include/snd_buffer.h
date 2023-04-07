#pragma once
#include "core_time.h"
#include "snd_channel.h"

typedef struct {
  f32 samples[SndChannel_Count];
} SndBufferFrame;

typedef struct {
  SndBufferFrame* frames;
  u32             frameCount, frameRate;
} SndBuffer;

typedef struct {
  const SndBufferFrame* frames;
  u32                   frameCount, frameRate;
} SndBufferView;

SndBufferView snd_buffer_view(SndBuffer);
SndBufferView snd_buffer_slice(SndBufferView, u32 offset, u32 count);
TimeDuration  snd_buffer_duration(SndBufferView);
f32           snd_buffer_frequency_max(SndBufferView);

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
