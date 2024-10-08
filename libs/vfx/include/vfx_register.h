#pragma once
#include "ecs_def.h"

enum {
  VfxOrder_StatCollect = -50,
  VfxOrder_Render      = +900,
};

/**
 * Register the ecs modules for the Vfx library.
 */
void vfx_register(EcsDef*);
