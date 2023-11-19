#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

#define scene_set_member_sets_max 8

ecs_comp_extern(SceneSetEnvComp);
ecs_comp_extern_public(SceneSetMemberComp) { StringHash sets[scene_set_member_sets_max]; };

/**
 * Query a set.
 */
bool               scene_set_contains(const SceneSetEnvComp*, StringHash set, EcsEntityId);
bool               scene_set_member_contains(const SceneSetMemberComp*, StringHash set);
u32                scene_set_count(const SceneSetEnvComp*, StringHash set);
EcsEntityId        scene_set_main(const SceneSetEnvComp*, StringHash set);
const EcsEntityId* scene_set_begin(const SceneSetEnvComp*, StringHash set);
const EcsEntityId* scene_set_end(const SceneSetEnvComp*, StringHash set);

/**
 * Modify a set.
 * NOTE: Deferred until the next 'SceneOrder_SetUpdate'.
 */
void scene_set_add(SceneSetEnvComp*, StringHash set, EcsEntityId);
void scene_set_remove(SceneSetEnvComp*, StringHash set, EcsEntityId);
void scene_set_clear(SceneSetEnvComp*, StringHash set);
