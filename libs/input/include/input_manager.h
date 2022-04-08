#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Global input manager component.
 */
ecs_comp_extern(InputManagerComp);

/**
 * Retrieve the entity of the active (focussed) window.
 * NOTE: Returns 0 when there is no active window.
 */
EcsEntityId input_active_window(const InputManagerComp*);

/**
 * Check if an input action was triggered this tick.
 */
bool input_triggered(const InputManagerComp*, String action);
bool input_triggered_hash(const InputManagerComp*, u32 actionHash);
