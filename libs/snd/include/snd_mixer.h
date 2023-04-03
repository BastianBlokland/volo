#pragma once
#include "ecs_module.h"
#include "snd_buffer.h"

/**
 * Global sound mixer.
 */
ecs_comp_extern(SndMixerComp);

/**
 * Controls.
 */
f32  snd_mixer_gain_get(const SndMixerComp*);
void snd_mixer_gain_set(SndMixerComp*, f32 gain);

/**
 * Query output device info.
 */
String snd_mixer_device_id(const SndMixerComp*);
String snd_mixer_device_state(const SndMixerComp*);
u64    snd_mixer_device_underruns(const SndMixerComp*);

/**
 * History ring-buffer for analysis / debug purposes.
 */
SndBufferView snd_mixer_history(const SndMixerComp*);
