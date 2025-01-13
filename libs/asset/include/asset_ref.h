#pragma once
#include "asset.h"
#include "ecs.h"

/**
 * Reference to an asset.
 */
typedef struct sAssetRef {
  StringHash  id;
  EcsEntityId entity;
} AssetRef;

/**
 * Reference to an entity in a level.
 */
typedef struct sAssetLevelRef {
  u32 persistentId;
} AssetLevelRef;

EcsEntityId asset_ref_resolve(EcsWorld*, AssetManagerComp*, const AssetRef*);
