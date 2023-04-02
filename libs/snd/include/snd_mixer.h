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
