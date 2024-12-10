#pragma once
#include "core_string.h"
#include "ecs.h"

/**
 * Asset reference.
 */
typedef struct sAssetRef {
  String      id;
  EcsEntityId entity;
} AssetRef;
