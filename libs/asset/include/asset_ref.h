#pragma once
#include "ecs.h"

/**
 * Asset reference.
 */
typedef struct sAssetRef {
  StringHash  id;
  EcsEntityId entity;
} AssetRef;
