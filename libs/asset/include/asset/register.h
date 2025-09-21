#pragma once
#include "ecs/def.h"

enum {
  AssetOrder_Init   = -1000,
  AssetOrder_Update = -900,
  AssetOrder_Deinit = 1000,
};

typedef struct {
  bool devSupport;
} AssetRegisterContext;

/**
 * Register the ecs modules for the Asset library.
 */
void asset_register(EcsDef*, const AssetRegisterContext*);
