#pragma once
#include "ecs_module.h"
#include "snd_channel.h"

typedef struct {
  f32 samples[SndChannel_Count];
} SndMixerFrame;

/**
 * Global sound mixer.
 */
ecs_comp_extern(SndMixerComp);
