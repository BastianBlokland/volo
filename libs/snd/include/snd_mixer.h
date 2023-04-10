#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "snd_buffer.h"
#include "snd_result.h"

/**
 * Global sound mixer.
 */
ecs_comp_extern(SndMixerComp);

/**
 * Objects.
 */
typedef u32 SndObjectId;

SndResult snd_object_new(SndMixerComp*, SndObjectId* outId);
SndResult snd_object_set_asset(SndMixerComp*, SndObjectId, EcsEntityId asset);

/**
 * Controls.
 */
f32  snd_mixer_gain_get(const SndMixerComp*);
void snd_mixer_gain_set(SndMixerComp*, f32 gain);

/**
 * Query output device info.
 */
String snd_mixer_device_id(const SndMixerComp*);
String snd_mixer_device_backend(const SndMixerComp*);
String snd_mixer_device_state(const SndMixerComp*);
u64    snd_mixer_device_underruns(const SndMixerComp*);

/**
 * Stats.
 */
u32          snd_mixer_objects_playing(const SndMixerComp*);
u32          snd_mixer_objects_allocated(const SndMixerComp*);
TimeDuration snd_mixer_render_duration(const SndMixerComp*);

/**
 * History ring-buffer for analysis / debug purposes.
 */
SndBufferView snd_mixer_history(const SndMixerComp*);
