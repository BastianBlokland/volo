#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneFootstepComp) {
  StringHash  jointA, jointB;
  EcsEntityId decalAsset;
};
