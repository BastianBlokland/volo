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

void          snd_buffer_clear(SndBuffer);
f32*          snd_buffer_samples(SndBuffer); // Raw access to the LRLRLR interleaved sample data.
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
 * Compute the peak magnitude of the sound.
 */
f32 snd_buffer_magnitude_peak(SndBufferView, SndChannel);

/**
 * Compute the RMS (aka quadratic mean) magnitude of the sound.
 * More info: https://en.wikipedia.org/wiki/Root_mean_square
 */
f32 snd_buffer_magnitude_rms(SndBufferView, SndChannel);

/**
 * Compute the amplitude per frequency of the sound.
 * The first output value represents 0hz and the last represents 'snd_buffer_frequency_max(view)'.
 * NOTE: Output buffer needs to be big enough to hold half the amount of input frames.
 * Pre-condition: bits_ispow2(view.frameCount).
 * Pre-condition: view.frameCount <= 8192.
 * Pre-condition: out[view.frameCount / 2].
 */
void snd_buffer_spectrum(SndBufferView, SndChannel, f32 outMagnitudes[]);
