#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum eSceneStatusType {
  SceneStatusType_Burning,
  SceneStatusType_Bleeding,
  SceneStatusType_Healing,
  SceneStatusType_Veteran,

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
f32    scene_status_move_speed(const SceneStatusComp*); // Move speed multiplier.
f32    scene_status_damage(const SceneStatusComp*);     // Damage multiplier.
String scene_status_name(SceneStatusType);

void scene_status_add(EcsWorld*, EcsEntityId target, SceneStatusType, EcsEntityId instigator);
void scene_status_add_many(EcsWorld*, EcsEntityId target, SceneStatusMask, EcsEntityId instigator);

void scene_status_remove(EcsWorld*, EcsEntityId target, SceneStatusType);
void scene_status_remove_many(EcsWorld*, EcsEntityId target, SceneStatusMask);
