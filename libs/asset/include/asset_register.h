#pragma once
#include "ecs_def.h"

enum {
  AssetOrder_Update = -900,
};

/**
 * Register the ecs modules for the Asset library.
 */
void asset_register(EcsDef*);
