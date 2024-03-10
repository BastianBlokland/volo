#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  SceneStatusType_Burning,
  SceneStatusType_Bleeding,

  SceneStatusType_Count,
} SceneStatusType;

typedef u8 SceneStatusMask;

ecs_comp_extern_public(SceneStatusComp) {
  SceneStatusMask supported;
  SceneStatusMask active;
  StringHash      effectJoint;
  TimeDuration    lastRefreshTime[SceneStatusType_Count];
  EcsEntityId     effectEntities[SceneStatusType_Count];
  EcsEntityId     instigators[SceneStatusType_Count];
};

ecs_comp_extern_public(SceneStatusRequestComp) {
  SceneStatusMask add;
  SceneStatusMask remove;
  EcsEntityId     instigators[SceneStatusType_Count];
};

bool   scene_status_active(const SceneStatusComp*, SceneStatusType);
String scene_status_name(SceneStatusType);

void scene_status_add(EcsWorld*, EcsEntityId target, SceneStatusType, EcsEntityId instigator);
void scene_status_remove(EcsWorld*, EcsEntityId target, SceneStatusType);
