#pragma once
#include "ecs_module.h"

#define scene_footstep_feet_max 2

ecs_comp_extern_public(SceneFootstepComp) {
  StringHash  jointNames[scene_footstep_feet_max];
  EcsEntityId decalAssets[scene_footstep_feet_max];
};
