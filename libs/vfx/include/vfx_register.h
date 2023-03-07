#pragma once
#include "ecs_def.h"

enum {
  VfxOrder_Update = 850,
};

/**
 * Register the ecs modules for the Vfx library.
 */
void vfx_register(EcsDef*);
