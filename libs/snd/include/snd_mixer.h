#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "snd_buffer.h"
#include "snd_result.h"

/**
 * Global sound mixer.
 */
ecs_comp_extern(SndMixerComp);

typedef u32 SndObjectId;

/**
 * Object apis.
 */
SndResult snd_object_new(SndMixerComp*, SndObjectId* outId);
String    snd_object_get_name(const SndMixerComp*, SndObjectId);
bool      snd_object_get_loading(const SndMixerComp*, SndObjectId);
u32       snd_object_get_frame_count(const SndMixerComp*, SndObjectId);
u32       snd_object_get_frame_rate(const SndMixerComp*, SndObjectId);
u8        snd_object_get_frame_channels(const SndMixerComp*, SndObjectId);
f64       snd_object_get_cursor(const SndMixerComp*, SndObjectId);
f32       snd_object_get_pitch(const SndMixerComp*, SndObjectId);
f32       snd_object_get_gain(const SndMixerComp*, SndObjectId, SndChannel);
SndResult snd_object_set_asset(SndMixerComp*, SndObjectId, EcsEntityId asset);
SndResult snd_object_set_pitch(SndMixerComp*, SndObjectId, f32 pitch);
SndResult snd_object_set_gain(SndMixerComp*, SndObjectId, SndChannel, f32 gain);

/**
 * Iterate through the active sound objects.
 * NOTE: Pass 'sentinel_u32' as the previous to get the first object, returns 'sentinel_u32' when
 * the end has been reached.
 */
SndObjectId snd_object_next(const SndMixerComp*, SndObjectId previousId);

/**
 * Global controls.
 */
f32       snd_mixer_gain_get(const SndMixerComp*);
SndResult snd_mixer_gain_set(SndMixerComp*, f32 gain);

/**
 * Stats.
 */
String        snd_mixer_device_id(const SndMixerComp*);
String        snd_mixer_device_backend(const SndMixerComp*);
String        snd_mixer_device_state(const SndMixerComp*);
u64           snd_mixer_device_underruns(const SndMixerComp*);
u32           snd_mixer_objects_playing(const SndMixerComp*);
u32           snd_mixer_objects_allocated(const SndMixerComp*);
TimeDuration  snd_mixer_render_duration(const SndMixerComp*);
SndBufferView snd_mixer_history(const SndMixerComp*);
