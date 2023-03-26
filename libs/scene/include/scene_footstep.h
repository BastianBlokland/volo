#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

#define scene_footstep_joint_max 2

ecs_comp_extern_public(SceneFootstepComp) {
  StringHash  jointNames[scene_footstep_joint_max];
  EcsEntityId decalAsset;
};
