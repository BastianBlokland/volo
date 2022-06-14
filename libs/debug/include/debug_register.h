#pragma once
#include "ecs_def.h"

enum {
  DebugOrder_PhysicsDebugDraw = 700,
  DebugOrder_ShapeRender      = 800,
  DebugOrder_TextRender       = 800,
};

/**
 * Register the ecs modules for the Debug library.
 */
void debug_register(EcsDef*);
