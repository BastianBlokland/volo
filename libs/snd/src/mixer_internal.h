#pragma once
#include "ecs_entity.h"
#include "snd_mixer.h"

#include "result_internal.h"

typedef u32 SndMixerId;

SndResult snd_mixer_obj_new(SndMixerComp*, SndMixerId* outId);
SndResult snd_mixer_obj_set_asset(SndMixerComp*, SndMixerId, EcsEntityId asset);
