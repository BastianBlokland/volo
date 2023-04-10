#pragma once
#include "ecs_entity.h"
#include "snd_mixer.h"
#include "snd_result.h"

typedef u32 SndObjectId;

SndResult snd_object_new(SndMixerComp*, SndObjectId* outId);
SndResult snd_object_set_asset(SndMixerComp*, SndObjectId, EcsEntityId asset);
