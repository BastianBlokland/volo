#pragma once
#include "ecs_def.h"

enum {
  SndOrder_Cleanup     = -100,
  SndOrder_Update      = 900,
  SndOrder_RenderBegin = 1000,
  SndOrder_RenderFill  = 1001,
  SndOrder_RenderEnd   = 1002,
};

/**
 * Register the ecs modules for the Sound library.
 */
void snd_register(EcsDef*);
