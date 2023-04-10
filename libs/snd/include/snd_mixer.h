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
SndResult    snd_object_new(SndMixerComp*, SndObjectId* outId);
SndResult    snd_object_set_asset(SndMixerComp*, SndObjectId, EcsEntityId asset);
String       snd_object_name(const SndMixerComp*, SndObjectId);
bool         snd_object_loading(const SndMixerComp*, SndObjectId);
TimeDuration snd_object_duration(const SndMixerComp*, SndObjectId);
u32          snd_object_frame_rate(const SndMixerComp*, SndObjectId);
u8           snd_object_frame_channels(const SndMixerComp*, SndObjectId);

/**
 * Iterate through the active sound objects.
 * NOTE: Pass 'sentinel_u32' as the previous to get the first object, returns 'sentinel_u32' when
 * the end has been reached.
 */
SndObjectId snd_object_next(const SndMixerComp*, SndObjectId previousId);

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
