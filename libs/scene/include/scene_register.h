#pragma once
#include "ecs_def.h"

enum {
  SceneOrder_TimeUpdate = -100,
};

/**
 * Register the ecs modules for the Scene library.
 */
void scene_register(EcsDef*);
