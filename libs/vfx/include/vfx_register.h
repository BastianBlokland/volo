#pragma once
#include "ecs_def.h"

enum {
  VfxOrder_Render = 800,
};

/**
 * Register the ecs modules for the Vfx library.
 */
void vfx_register(EcsDef*);
