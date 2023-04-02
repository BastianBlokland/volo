#pragma once
#include "ecs_module.h"
#include "snd_channel.h"

typedef struct {
  f32 samples[SndChannel_Count];
} SndMixerFrame;

typedef struct {
  SndMixerFrame* frames;
  usize          frameCount;
} SndMixerView;

/**
 * Global sound mixer.
 */
ecs_comp_extern(SndMixerComp);

/**
 * History ring-buffer for analysis / debug purposes.
 */
SndMixerView snd_mixer_history(const SndMixerComp*);

/**
 * Sample the view at the given fraction.
 * Pre-condition: frac >= 0.0 && frac <= 1.0.
 * Pre-condition: view.frameCount >= 2.
 */
f32 snd_mixer_sample(SndMixerView, SndChannel, f32 frac);
