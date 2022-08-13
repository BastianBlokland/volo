#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Global selection component.
 */
ecs_comp_extern(SceneSelectionComp);

/**
 * Retrieve the main selected entity or 0 if no entity is currently selected.
 */
EcsEntityId scene_selection_main(const SceneSelectionComp*);

/**
 * Check if the given entity is currently selected.
 */
bool scene_selection_contains(const SceneSelectionComp*, EcsEntityId);

/**
 * Check if the selection is currently empty.
 */
bool scene_selection_empty(const SceneSelectionComp*);

/**
 * Request the given entity to be selected.
 * NOTE: Is deferred until the next 'SceneOrder_SelectionUpdate'.
 */
void scene_selection_add(SceneSelectionComp*, EcsEntityId);

/**
 * Request to clear the selection (if any).
 * NOTE: Is deferred until the next 'SceneOrder_SelectionUpdate'.
 */
void scene_selection_clear(SceneSelectionComp*);
