#pragma once
#include "ecs/def.h"

enum {
  InputOrder_Read = -900,
};

/**
 * Register the ecs modules for the Input library.
 */
void input_register(EcsDef*);
