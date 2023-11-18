#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

#define scene_set_member_sets_max 4

ecs_comp_extern(SceneSetEnvComp);

ecs_comp_extern_public(SceneSetMemberComp) { StringHash sets[scene_set_member_sets_max]; };

/**
 * Check if the target entity is part of the given set.
 */
bool scene_set_contains(const SceneSetEnvComp*, StringHash set, EcsEntityId);

/**
 * Retrieve the members of the given set.
 */
const EcsEntityId* scene_set_begin(const SceneSetEnvComp*, StringHash set);
const EcsEntityId* scene_set_end(const SceneSetEnvComp*, StringHash set);
