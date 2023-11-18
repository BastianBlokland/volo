#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

#define scene_set_member_sets_max 4

ecs_comp_extern(SceneSetEnvComp);

ecs_comp_extern_public(SceneSetMemberComp) { StringHash sets[scene_set_member_sets_max]; };
