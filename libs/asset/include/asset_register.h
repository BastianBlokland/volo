#pragma once
#include "ecs_def.h"

enum {
  AssetOrder_Init   = -1000,
  AssetOrder_Update = -900,
  AssetOrder_Deinit = 1000,
};

/**
 * Register the ecs modules for the Asset library.
 */
void asset_register(EcsDef*);
