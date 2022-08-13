#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Global selection component.
 */
ecs_comp_extern(SceneSelectionComp);

/**
 * Check the current selection.
 */
EcsEntityId        scene_selection_main(const SceneSelectionComp*);
const EcsEntityId* scene_selection_begin(const SceneSelectionComp*);
const EcsEntityId* scene_selection_end(const SceneSelectionComp*);
u32                scene_selection_count(const SceneSelectionComp*);
bool               scene_selection_contains(const SceneSelectionComp*, EcsEntityId);
bool               scene_selection_empty(const SceneSelectionComp*);

/**
 * Modify the current selection..
 * NOTE: Deferred until the next 'SceneOrder_SelectionUpdate'.
 */
void scene_selection_add(SceneSelectionComp*, EcsEntityId);
void scene_selection_remove(SceneSelectionComp*, EcsEntityId);
void scene_selection_clear(SceneSelectionComp*);
