#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneAttachmentComp) {
  EcsEntityId target;
  StringHash  jointName;
  u32         jointIndex;
};

void scene_attach_to_joint(EcsWorld*, EcsEntityId, EcsEntityId target, u32 jointIndex);
void scene_attach_to_joint_name(EcsWorld*, EcsEntityId, EcsEntityId target, StringHash jointName);
