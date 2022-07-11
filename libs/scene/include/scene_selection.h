#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Global selection component.
 */
ecs_comp_extern(SceneSelectionComp);

/**
 * Retrieve the EntityId of the selected entity or 0 if no entity is currently selected.
 */
EcsEntityId scene_selected(const SceneSelectionComp*);

/**
 * Request the given entity to be selected.
 * NOTE: Is deferred until the next 'SceneOrder_SelectionUpdate'.
 */
void scene_select(SceneSelectionComp*, EcsEntityId);

/**
 * Request to clear the selection (if any).
 * NOTE: Is deferred until the next 'SceneOrder_SelectionUpdate'.
 */
void scene_deselect(SceneSelectionComp*);
