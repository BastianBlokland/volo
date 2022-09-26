#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneAttackComp) {
  TimeDuration lastAttackTime;
  TimeDuration attackInterval;
  EcsEntityId  attackTarget;
  EcsEntityId  projectileGraphic;
};
