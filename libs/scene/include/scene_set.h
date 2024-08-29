#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Maximum amount of sets that a single member can be in.
 */
#define scene_set_member_max_sets 8

typedef enum {
  SceneSetFlags_None     = 0,
  SceneSetFlags_MakeMain = 1 << 0,
} SceneSetFlags;

/**
 * Well-known sets.
 */
extern StringHash g_sceneSetUnit;
extern StringHash g_sceneSetSelected;

/**
 * Set environment.
 */
ecs_comp_extern(SceneSetEnvComp);
ecs_comp_extern(SceneSetMemberComp);

void scene_set_member_create(EcsWorld*, EcsEntityId, const StringHash* sets, u32 setCount);
bool scene_set_member_contains(const SceneSetMemberComp*, StringHash set);
u32  scene_set_member_all(
     const SceneSetMemberComp*, StringHash out[PARAM_ARRAY_SIZE(scene_set_member_max_sets)]);

/**
 * Query a set.
 */
bool               scene_set_contains(const SceneSetEnvComp*, StringHash set, EcsEntityId);
u32                scene_set_count(const SceneSetEnvComp*, StringHash set);
EcsEntityId        scene_set_main(const SceneSetEnvComp*, StringHash set);
const EcsEntityId* scene_set_begin(const SceneSetEnvComp*, StringHash set);
const EcsEntityId* scene_set_end(const SceneSetEnvComp*, StringHash set);

/**
 * Modify a set.
 * NOTE: Deferred until the next 'SceneOrder_SetUpdate'.
 */
void scene_set_add(SceneSetEnvComp*, StringHash set, EcsEntityId, SceneSetFlags);
void scene_set_remove(SceneSetEnvComp*, StringHash set, EcsEntityId);
void scene_set_clear(SceneSetEnvComp*, StringHash set);
