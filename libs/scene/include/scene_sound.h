#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(SceneSoundComp) {
  EcsEntityId asset; // Sound asset.
  f32         pitch, gain;
  bool        looping;
};

ecs_comp_extern_public(SceneSoundListenerComp);
