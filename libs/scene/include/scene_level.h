#pragma once
#include "ecs_module.h"

/**
 * Delete level entities.
 */
void scene_level_unload(EcsWorld*);

/**
 * Save the current scene as a level with the given id.
 */
void scene_level_save(EcsWorld*, String levelId);
