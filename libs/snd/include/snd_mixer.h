#pragma once
#include "ecs_module.h"
#include "snd_buffer.h"

/**
 * Global sound mixer.
 */
ecs_comp_extern(SndMixerComp);

/**
 * History ring-buffer for analysis / debug purposes.
 */
SndBufferView snd_mixer_history(const SndMixerComp*);
