#pragma once
#include "ecs_def.h"

enum {
  DebugOrder_ShapeRender = 800,
};

/**
 * Register the ecs modules for the Debug library.
 */
void debug_register(EcsDef*);
