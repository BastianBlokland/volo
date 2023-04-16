#pragma once
#include "ecs_def.h"

enum {
  SndOrder_Cleanup = -100,
  SndOrder_Render  = 1000,
};

/**
 * Register the ecs modules for the Sound library.
 */
void snd_register(EcsDef*);
