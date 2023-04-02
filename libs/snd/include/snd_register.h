#pragma once
#include "ecs_def.h"

enum {
  SndOrder_Mix = 1000,
};

/**
 * Register the ecs modules for the Sound library.
 */
void snd_register(EcsDef*);
