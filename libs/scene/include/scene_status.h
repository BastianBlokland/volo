#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  SceneStatusType_Burning,

  SceneStatusType_Count,
} SceneStatusType;

typedef u8 SceneStatusMask;

ecs_comp_extern_public(SceneStatusComp) { SceneStatusMask active; };

ecs_comp_extern_public(SceneStatusRequestComp) {
  SceneStatusMask add;
  SceneStatusMask remove;
};

bool scene_status_active(const SceneStatusComp*, SceneStatusType);

void scene_status_add(EcsWorld*, EcsEntityId, SceneStatusType);
void scene_status_remove(EcsWorld*, EcsEntityId, SceneStatusType);
