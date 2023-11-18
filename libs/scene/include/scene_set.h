#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

#define scene_set_member_sets_max 8

ecs_comp_extern(SceneSetEnvComp);

ecs_comp_extern_public(SceneSetMemberComp) { StringHash sets[scene_set_member_sets_max]; };

bool scene_set_contains(const SceneSetEnvComp*, StringHash set, EcsEntityId);
bool scene_set_member_contains(const SceneSetMemberComp*, StringHash set);

u32                scene_set_count(const SceneSetEnvComp*, StringHash set);
const EcsEntityId* scene_set_begin(const SceneSetEnvComp*, StringHash set);
const EcsEntityId* scene_set_end(const SceneSetEnvComp*, StringHash set);
