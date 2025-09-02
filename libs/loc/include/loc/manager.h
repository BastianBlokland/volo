#pragma once
#include "ecs/module.h"

/**
 * Global locale manager component.
 */
ecs_comp_extern(LocManagerComp);

/**
 * Initialize the locale manager.
 */
LocManagerComp* loc_manager_init(EcsWorld*, String preferredLocale);
