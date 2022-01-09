#pragma once
#include "ecs_def.h"

enum {
  GapOrder_PlatformUpdate = -1000,
  GapOrder_WindowUpdate   = -999,
};

/**
 * Register the ecs modules for the GuiApplicationProtocol library.
 */
void gap_register(EcsDef*);
