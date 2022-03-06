#pragma once
#include "ecs_def.h"

enum {
  RendOrder_DrawClear   = -100,
  RendOrder_DrawCollect = 900,
  RendOrder_DrawExecute = 1000,
};

/**
 * Register the ecs modules for the Renderer library.
 */
void rend_register(EcsDef*);
