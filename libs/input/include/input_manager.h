#pragma once
#include "ecs_module.h"

/**
 * Global input manager component.
 */
ecs_comp_extern(InputManagerComp);

/**
 * Check if an input action was triggered this tick.
 */
bool input_triggered(const InputManagerComp*, String action);
bool input_triggered_hash(const InputManagerComp*, u32 actionHash);
